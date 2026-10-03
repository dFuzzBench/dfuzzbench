/*
---------------------------------------------------------------------------
Open Asset Import Library (assimp)
---------------------------------------------------------------------------

Copyright (c) 2006-2026, assimp team

All rights reserved.

Redistribution and use of this software in source and binary forms,
with or without modification, are permitted provided that the following
conditions are met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the
  following disclaimer in the documentation and/or other
  materials provided with the distribution.

* Neither the name of the assimp team, nor the names of its
  contributors may be used to endorse or promote products
  derived from this software without specific prior
  written permission of the assimp team.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
---------------------------------------------------------------------------
*/

/** @file Implementation of the STL importer class */

#ifndef ASSIMP_BUILD_NO_NFF_IMPORTER

// internal headers
#include "NFFLoader.h"
#include <assimp/ParsingUtils.h>
#include <assimp/RemoveComments.h>
#include <assimp/StandardShapes.h>
#include <assimp/fast_atof.h>
#include <assimp/importerdesc.h>
#include <assimp/qnan.h>
#include <assimp/scene.h>
#include <assimp/DefaultLogger.hpp>
#include <assimp/IOSystem.hpp>
#include <memory>

namespace Assimp {

static constexpr aiImporterDesc desc = {
    "Neutral File Format Importer",
    "",
    "",
    "",
    aiImporterFlags_SupportBinaryFlavour,
    0,
    0,
    0,
    0,
    "enff nff"
};

// ------------------------------------------------------------------------------------------------
// Returns whether the class can handle the format of the given file.
bool NFFImporter::CanRead(const std::string & pFile, IOSystem * /*pIOHandler*/, bool /*checkSig*/) const {
    return SimpleExtensionCheck(pFile, "nff", "enff");
}

// ------------------------------------------------------------------------------------------------
// Get the list of all supported file extensions
const aiImporterDesc *NFFImporter::GetInfo() const {
    return &desc;
}

// ------------------------------------------------------------------------------------------------
#define AI_NFF_PARSE_FLOAT(f) \
    SkipSpaces(&sz, lineEnd);          \
    if (!IsLineEnd(*sz)) sz = fast_atoreal_move(sz, (ai_real &)f);

// ------------------------------------------------------------------------------------------------
#define AI_NFF_PARSE_TRIPLE(v) \
    AI_NFF_PARSE_FLOAT(v[0])   \
    AI_NFF_PARSE_FLOAT(v[1])   \
    AI_NFF_PARSE_FLOAT(v[2])

// ------------------------------------------------------------------------------------------------
#define AI_NFF_PARSE_SHAPE_INFORMATION()                     \
    aiVector3D center, radius(1.0f, get_qnan(), get_qnan()); \
    AI_NFF_PARSE_TRIPLE(center);                             \
    AI_NFF_PARSE_TRIPLE(radius);                             \
    if (is_qnan(radius.z)) radius.z = radius.x;              \
    if (is_qnan(radius.y)) radius.y = radius.x;              \
    curMesh.radius = radius;                                 \
    curMesh.center = center;

// ------------------------------------------------------------------------------------------------
#define AI_NFF2_GET_NEXT_TOKEN()                                            \
    do {                                                                    \
        if (!GetNextLine(buffer, line)) {                                   \
            ASSIMP_LOG_WARN("NFF2: Unexpected EOF, can't read next token"); \
            break;                                                          \
        }                                                                   \
        SkipSpaces(line, &sz, lineEnd);                                              \
    } while (IsLineEnd(*sz))

// ------------------------------------------------------------------------------------------------
// Loads the material table for the NFF2 file format from an external file
void NFFImporter::LoadNFF2MaterialTable(std::vector<ShadingInfo> &output,
        const std::string &path, IOSystem *pIOHandler) {
    std::unique_ptr<IOStream> file(pIOHandler->Open(path, "rb"));

    // Check whether we can read from the file
    if (!file) {
        ASSIMP_LOG_ERROR("NFF2: Unable to open material library ", path, ".");
        return;
    }

    // get the size of the file
    const unsigned int m = (unsigned int)file->FileSize();

    // allocate storage and copy the contents of the file to a memory buffer
    // (terminate it with zero)
    std::vector<char> mBuffer2(m + 1);
    TextFileToBuffer(file.get(), mBuffer2);
    const char *buffer = &mBuffer2[0];

    // First of all: remove all comments from the file
    CommentRemover::RemoveLineComments("//", &mBuffer2[0]);

    // The file should start with the magic sequence "mat"
    if (!TokenMatch(buffer, "mat", 3)) {
        ASSIMP_LOG_ERROR("NFF2: Not a valid material library ", path, ".");
        return;
    }

    ShadingInfo *curShader = nullptr;

    // No read the file line per line
    char line[4096];
    const char *sz, *lineEnd = &line[2095]+1;
    while (GetNextLine(buffer, line)) {
        SkipSpaces(line, &sz, lineEnd);

        // 'version' defines the version of the file format
        if (TokenMatch(sz, "version", 7)) {
            ASSIMP_LOG_INFO("NFF (Sense8) material library file format: ", std::string(sz));
        }
        // 'matdef' starts a new material in the file
        else if (TokenMatch(sz, "matdef", 6)) {
            // add a new material to the list
            output.emplace_back();
            curShader = &output.back();

            // parse the name of the material
        } else if (!TokenMatch(sz, "valid", 5)) {
            // check whether we have an active material at the moment
            if (!IsLineEnd(*sz)) {
                if (!curShader) {
                    ASSIMP_LOG_ERROR("NFF2 material library: Found element ", sz, "but there is no active material");
                    continue;
                }
            } else
                continue;

            // now read the material property and determine its type
            aiColor3D c;
            if (TokenMatch(sz, "ambient", 7)) {
                AI_NFF_PARSE_TRIPLE(c);
                curShader->ambient = c;
            } else if (TokenMatch(sz, "diffuse", 7) || TokenMatch(sz, "ambientdiffuse", 14) /* correct? */) {
                AI_NFF_PARSE_TRIPLE(c);
                curShader->diffuse = curShader->ambient = c;
            } else if (TokenMatch(sz, "specular", 8)) {
                AI_NFF_PARSE_TRIPLE(c);
                curShader->specular = c;
            } else if (TokenMatch(sz, "emission", 8)) {
                AI_NFF_PARSE_TRIPLE(c);
                curShader->emissive = c;
            } else if (TokenMatch(sz, "shininess", 9)) {
                AI_NFF_PARSE_FLOAT(curShader->shininess);
            } else if (TokenMatch(sz, "opacity", 7)) {
                AI_NFF_PARSE_FLOAT(curShader->opacity);
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Imports the given file into the given scene structure.
void NFFImporter::InternReadFile(const std::string &file, aiScene *pScene, IOSystem *pIOHandler) {
    std::unique_ptr<IOStream> stream(pIOHandler->Open(file, "rb"));
    if (!stream) {
        throw DeadlyImportError("Failed to open NFF file ", file, ".");
    }

    // allocate storage and copy the contents of the file to a memory buffer
    // (terminate it with zero)
    std::vector<char> mBuffer2;
    TextFileToBuffer(stream.get(), mBuffer2);
    const char *buffer = &mBuffer2[0];

    // mesh arrays - separate here to make the handling of the pointers below easier.
    std::vector<MeshInfo> meshes;
    std::vector<MeshInfo> meshesWithNormals;
    std::vector<MeshInfo> meshesWithUVCoords;
    std::vector<MeshInfo> meshesLocked;

    char line[4096];
    const char *lineEnd = &line[4096];
    const char *sz;


    // camera parameters
    aiVector3D camPos, camUp(0.f, 1.f, 0.f), camLookAt(0.f, 0.f, 1.f);
    ai_real angle = 45.f;
    aiVector2D resolution;

    bool hasCam = false;

    MeshInfo *currentMeshWithNormals = nullptr;
    MeshInfo *currentMesh = nullptr;
    MeshInfo *currentMeshWithUVCoords = nullptr;

    ShadingInfo s; // current material info

    // degree of tessellation
    unsigned int iTesselation = 4;

    // some temporary variables we need to parse the file
    unsigned int sphere = 0,
                 cylinder = 0,
                 cone = 0,
                 numNamed = 0,
                 dodecahedron = 0,
                 octahedron = 0,
                 tetrahedron = 0,
                 hexahedron = 0;

    // lights imported from the file
    std::vector<Light> lights;

    // check whether this is the NFF2 file format
    if (TokenMatch(buffer, "nff", 3)) {
        const ai_real qnan = get_qnan();
        const aiColor4D cQNAN = aiColor4D(qnan, 0.f, 0.f, 1.f);
        const aiVector3D vQNAN = aiVector3D(qnan, 0.f, 0.f);

        // another NFF file format ... just a raw parser has been implemented
        // no support for further details, I don't think it is worth the effort
        // http://ozviz.wasp.uwa.edu.au/~pbourke/dataformats/nff/nff2.html
        // http://www.netghost.narod.ru/gff/graphics/summary/sense8.htm

        // First of all: remove all comments from the file
        CommentRemover::RemoveLineComments("//", &mBuffer2[0]);

        while (GetNextLine(buffer, line)) {
            SkipSpaces(line, &sz, lineEnd);
            if (TokenMatch(sz, "version", 7)) {
                ASSIMP_LOG_INFO("NFF (Sense8) file format: ", sz);
            } else if (TokenMatch(sz, "viewpos", 7)) {
                AI_NFF_PARSE_TRIPLE(camPos);
                hasCam = true;
            } else if (TokenMatch(sz, "viewdir", 7)) {
                AI_NFF_PARSE_TRIPLE(camLookAt);
                hasCam = true;
            }
            // This starts a new object section
            else if (!IsSpaceOrNewLine(*sz)) {
                unsigned int subMeshIdx = 0;

                // read the name of the object, skip all spaces
                // at the end of it.
                const char *sz3 = sz;
                while (!IsSpaceOrNewLine(*sz))
                    ++sz;
                std::string objectName = std::string(sz3, (unsigned int)(sz - sz3));

                const unsigned int objStart = (unsigned int)meshes.size();

                // There could be a material table in a separate file
                std::vector<ShadingInfo> materialTable;
                while (true) {
                    AI_NFF2_GET_NEXT_TOKEN();

                    // material table - an external file
                    if (TokenMatch(sz, "mtable", 6)) {
                        SkipSpaces(&sz, lineEnd);
                        sz3 = sz;
                        while (!IsSpaceOrNewLine(*sz))
                            ++sz;
                        const unsigned int diff = (unsigned int)(sz - sz3);
                        if (!diff)
                            ASSIMP_LOG_WARN("NFF2: Found empty mtable token");
                        else {
                            // The material table has the file extension .mat.
                            // If it is not there, we need to append it
                            std::string path = std::string(sz3, diff);
                            if (std::string::npos == path.find_last_of(".mat")) {
                                path.append(".mat");
                            }

                            // Now extract the working directory from the path to
                            // this file and append the material library filename
                            // to it.
                            std::string::size_type sepPos;
                            if ((std::string::npos == (sepPos = path.find_last_of('\\')) || !sepPos) &&
                                    (std::string::npos == (sepPos = path.find_last_of('/')) || !sepPos)) {
                                sepPos = file.find_last_of('\\');
                                if (std::string::npos == sepPos) {
                                    sepPos = file.find_last_of('/');
                                }
                                if (std::string::npos != sepPos) {
                                    path = file.substr(0, sepPos + 1) + path;
                                }
                            }
                            LoadNFF2MaterialTable(materialTable, path, pIOHandler);
                        }
                    } else
                        break;
                }

                // read the number of vertices
                unsigned int num = strtoul10(sz, &sz);

                // temporary storage
                std::vector<aiColor4D> tempColors;
                std::vector<aiVector3D> tempPositions, tempTextureCoords, tempNormals;

                bool hasNormals = false, hasUVs = false, hasColor = false;

                tempPositions.reserve(num);
                tempColors.reserve(num);
                tempNormals.reserve(num);
                tempTextureCoords.reserve(num);
                for (unsigned int i = 0; i < num; ++i) {
                    AI_NFF2_GET_NEXT_TOKEN();
                    aiVector3D v;
                    AI_NFF_PARSE_TRIPLE(v);
                    tempPositions.push_back(v);

                    // parse all other attributes in the line
                    while (true) {
                        SkipSpaces(&sz, lineEnd);
                        if (IsLineEnd(*sz)) break;

                        // color definition
                        if (TokenMatch(sz, "0x", 2)) {
                            hasColor = true;
                            unsigned int numIdx = strtoul16(sz, &sz);
                            aiColor4D clr;
                            clr.a = 1.f;

                            // 0xRRGGBB
                            clr.r = ((numIdx >> 16u) & 0xff) / 255.f;
                            clr.g = ((numIdx >> 8u) & 0xff) / 255.f;
                            clr.b = ((numIdx)&0xff) / 255.f;
                            tempColors.push_back(clr);
                        }
                        // normal vector
                        else if (TokenMatch(sz, "norm", 4)) {
                            hasNormals = true;
                            AI_NFF_PARSE_TRIPLE(v);
                            tempNormals.push_back(v);
                        }
                        // UV coordinate
                        else if (TokenMatch(sz, "uv", 2)) {
                            hasUVs = true;
                            AI_NFF_PARSE_FLOAT(v.x);
                            AI_NFF_PARSE_FLOAT(v.y);
                            v.z = 0.f;
                            tempTextureCoords.push_back(v);
                        }
                    }

                    // fill in dummies for all attributes that have not been set
                    if (tempNormals.size() != tempPositions.size())
                        tempNormals.push_back(vQNAN);

                    if (tempTextureCoords.size() != tempPositions.size())
                        tempTextureCoords.push_back(vQNAN);

                    if (tempColors.size() != tempPositions.size())
                        tempColors.push_back(cQNAN);
                }

                AI_NFF2_GET_NEXT_TOKEN();
                if (!num)
                    throw DeadlyImportError("NFF2: There are zero vertices");
                num = strtoul10(sz, &sz);

                std::vector<unsigned int> tempIdx;
                tempIdx.reserve(10);
                for (unsigned int i = 0; i < num; ++i) {
                    AI_NFF2_GET_NEXT_TOKEN();
                    SkipSpaces(line, &sz, lineEnd);
                    unsigned int numIdx = strtoul10(sz, &sz);

                    // read all faces indices
                    if (numIdx) {
                        tempIdx.resize(numIdx);

                        for (unsigned int a = 0; a < numIdx; ++a) {
                            SkipSpaces(sz, &sz, lineEnd);
                            unsigned int m = strtoul10(sz, &sz);
                            if (m >= (unsigned int)tempPositions.size()) {
                                ASSIMP_LOG_ERROR("NFF2: Vertex index overflow");
                                m = 0;
                            }
                            tempIdx[a] = m;
                        }
                    }

                    // build a temporary shader object for the face.
                    ShadingInfo shader;
                    unsigned int matIdx = 0;

                    // white material color - we have vertex colors
                    shader.color = aiColor3D(1.f, 1.f, 1.f);
                    aiColor4D c = aiColor4D(1.f, 1.f, 1.f, 1.f);
                    while (true) {
                        SkipSpaces(sz, &sz, lineEnd);
                        if (IsLineEnd(*sz)) break;

                        // per-polygon colors
                        if (TokenMatch(sz, "0x", 2)) {
                            hasColor = true;
                            const char *sz2 = sz;
                            numIdx = strtoul16(sz, &sz);
                            const unsigned int diff = (unsigned int)(sz - sz2);

                            // 0xRRGGBB
                            if (diff > 3) {
                                c.r = ((numIdx >> 16u) & 0xff) / 255.f;
                                c.g = ((numIdx >> 8u) & 0xff) / 255.f;
                                c.b = ((numIdx)&0xff) / 255.f;
                            }
                            // 0xRGB
                            else {
                                c.r = ((numIdx >> 8u) & 0xf) / 16.f;
                                c.g = ((numIdx >> 4u) & 0xf) / 16.f;
                                c.b = ((numIdx)&0xf) / 16.f;
                            }
                        }
                        // TODO - implement texture mapping here
#if 0
                        // mirror vertex texture coordinate?
                        else if (TokenMatch(sz,"mirror",6))
                        {
                        }
                        // texture coordinate scaling
                        else if (TokenMatch(sz,"scale",5))
                        {
                        }
                        // texture coordinate translation
                        else if (TokenMatch(sz,"trans",5))
                        {
                        }
                        // texture coordinate rotation angle
                        else if (TokenMatch(sz,"rot",3))
                        {
                        }
#endif

                        // texture file name for this polygon + mapping information
                        else if ('_' == sz[0]) {
                            // get mapping information
                            switch (sz[1]) {
                                case 'v':
                                case 'V':

                                    shader.shaded = false;
                                    break;

                                case 't':
                                case 'T':
                                case 'u':
                                case 'U':

                                    ASSIMP_LOG_WARN("Unsupported NFF2 texture attribute: trans");
                            };
                            if (!sz[1] || '_' != sz[2]) {
                                ASSIMP_LOG_WARN("NFF2: Expected underscore after texture attributes");
                                continue;
                            }
                            const char *sz2 = sz + 3;
                            while (!IsSpaceOrNewLine(*sz))
                                ++sz;
                            const unsigned int diff = (unsigned int)(sz - sz2);
                            if (diff) shader.texFile = std::string(sz2, diff);
                        }

                        // Two-sided material?
                        else if (TokenMatch(sz, "both", 4)) {
                            shader.twoSided = true;
                        }

                        // Material ID?
                        else if (!materialTable.empty() && TokenMatch(sz, "matid", 5)) {
                            SkipSpaces(&sz, lineEnd);
                            matIdx = strtoul10(sz, &sz);
                            if (matIdx >= materialTable.size()) {
                                ASSIMP_LOG_ERROR("NFF2: Material index overflow.");
                                matIdx = 0;
                            }

                            // now combine our current shader with the shader we
                            // read from the material table.
                            ShadingInfo &mat = materialTable[matIdx];
                            shader.ambient = mat.ambient;
                            shader.diffuse = mat.diffuse;
                            shader.emissive = mat.emissive;
                            shader.opacity = mat.opacity;
                            shader.specular = mat.specular;
                            shader.shininess = mat.shininess;
                        } else
                            SkipToken(sz, lineEnd);
                    }

                    // search the list of all shaders we have for this object whether
                    // there is an identical one. In this case, we append our mesh
                    // data to it.
                    MeshInfo *mesh = nullptr;
                    for (std::vector<MeshInfo>::iterator it = meshes.begin() + objStart, end = meshes.end();
                            it != end; ++it) {
                        if ((*it).shader == shader && (*it).matIndex == matIdx) {
                            // we have one, we can append our data to it
                            mesh = &(*it);
                        }
                    }
                    if (!mesh) {
                        meshes.emplace_back(PatchType_Simple, false);
                        mesh = &meshes.back();
                        mesh->matIndex = matIdx;

                        // We need to add a new mesh to the list. We assign
                        // an unique name to it to make sure the scene will
                        // pass the validation step for the moment.
                        // TODO: fix naming of objects in the scene-graph later
                        if (objectName.length()) {
                            const size_t copyLen = objectName.length() < (sizeof(mesh->name) - 1u) ? objectName.length() : (sizeof(mesh->name) - 1u);
                            ::memcpy(mesh->name, objectName.c_str(), copyLen);
                            mesh->name[copyLen] = '\0';
                            if (copyLen < sizeof(mesh->name) - 1u) {
                                ASSIMP_itoa10(&mesh->name[copyLen], static_cast<unsigned int>(sizeof(mesh->name) - copyLen), subMeshIdx++);
                            }
                        }

                        // copy the shader to the mesh.
                        mesh->shader = shader;
                    }

                    // fill the mesh with data
                    if (!tempIdx.empty()) {
                        mesh->faces.push_back((unsigned int)tempIdx.size());
                        for (std::vector<unsigned int>::const_iterator it = tempIdx.begin(), end = tempIdx.end();
                                it != end; ++it) {
                            unsigned int m = *it;

                            // copy colors -vertex color specifications override polygon color specifications
                            if (hasColor) {
                                const aiColor4D &clr = tempColors[m];
                                mesh->colors.push_back((is_qnan(clr.r) ? c : clr));
                            }

                            // positions should always be there
                            mesh->vertices.push_back(tempPositions[m]);

                            // copy normal vectors
                            if (hasNormals)
                                mesh->normals.push_back(tempNormals[m]);

                            // copy texture coordinates
                            if (hasUVs)
                                mesh->uvs.push_back(tempTextureCoords[m]);
                        }
                    }
                }
                if (!num) throw DeadlyImportError("NFF2: There are zero faces");
            }
        }
        camLookAt = camLookAt + camPos;
    } else // "Normal" Neutral file format that is quite more common
    {
        while (GetNextLine(buffer, line)) {
            sz = line;
            if ('p' == line[0] || TokenMatch(sz, "tpp", 3)) {
                MeshInfo *out = nullptr;

                // 'tpp' - texture polygon patch primitive
                if ('t' == line[0]) {
                    currentMeshWithUVCoords = nullptr;
                    for (auto &mesh : meshesWithUVCoords) {
                        if (mesh.shader == s) {
                            currentMeshWithUVCoords = &mesh;
                            break;
                        }
                    }

                    if (!currentMeshWithUVCoords) {
                        meshesWithUVCoords.emplace_back(PatchType_UVAndNormals);
                        currentMeshWithUVCoords = &meshesWithUVCoords.back();
                        currentMeshWithUVCoords->shader = s;
                    }
                    out = currentMeshWithUVCoords;
                }
                // 'pp' - polygon patch primitive
                else if ('p' == line[1]) {
                    currentMeshWithNormals = nullptr;
                    for (auto &mesh : meshesWithNormals) {
                        if (mesh.shader == s) {
                            currentMeshWithNormals = &mesh;
                            break;
                        }
                    }

                    if (!currentMeshWithNormals) {
                        meshesWithNormals.emplace_back(PatchType_Normals);
                        currentMeshWithNormals = &meshesWithNormals.back();
                        currentMeshWithNormals->shader = s;
                    }
                    sz = &line[2];
                    out = currentMeshWithNormals;
                }
                // 'p' - polygon primitive
                else {
                    currentMesh = nullptr;
                    for (auto &mesh : meshes) {
                        if (mesh.shader == s) {
                            currentMesh = &mesh;
                            break;
                        }
                    }

                    if (!currentMesh) {
                        meshes.emplace_back(PatchType_Simple);
                        currentMesh = &meshes.back();
                        currentMesh->shader = s;
                    }
                    sz = &line[1];
                    out = currentMesh;
                }
                SkipSpaces(sz, &sz, lineEnd);
                unsigned int m = strtoul10(sz);

                // ---- flip the face order
                out->vertices.resize(out->vertices.size() + m);
                if (out != currentMesh) {
                    out->normals.resize(out->vertices.size());
                }
                if (out == currentMeshWithUVCoords) {
                    out->uvs.resize(out->vertices.size());
                }
                for (unsigned int n = 0; n < m; ++n) {
                    if (!GetNextLine(buffer, line)) {
                        ASSIMP_LOG_ERROR("NFF: Unexpected EOF was encountered. Patch definition incomplete");
                        continue;
                    }

                    aiVector3D v;
                    sz = &line[0];
                    AI_NFF_PARSE_TRIPLE(v);
                    out->vertices[out->vertices.size() - n - 1] = v;

                    if (out != currentMesh) {
                        AI_NFF_PARSE_TRIPLE(v);
                        out->normals[out->vertices.size() - n - 1] = v;
                    }
                    if (out == currentMeshWithUVCoords) {
                        // FIX: in one test file this wraps over multiple lines
                        SkipSpaces(&sz, lineEnd);
                        if (IsLineEnd(*sz)) {
                            GetNextLine(buffer, line);
                            sz = line;
                        }
                        AI_NFF_PARSE_FLOAT(v.x);
                        SkipSpaces(&sz, lineEnd);
                        if (IsLineEnd(*sz)) {
                            GetNextLine(buffer, line);
                            sz = line;
                        }
                        AI_NFF_PARSE_FLOAT(v.y);
                        v.y = 1.f - v.y;
                        out->uvs[out->vertices.size() - n - 1] = v;
                    }
                }
                out->faces.push_back(m);
            }
            // 'f' - shading information block
            else if (TokenMatch(sz, "f", 1)) {
                ai_real d;

                // read the RGB colors
                AI_NFF_PARSE_TRIPLE(s.color);

                // read the other properties
                AI_NFF_PARSE_FLOAT(s.diffuse.r);
                AI_NFF_PARSE_FLOAT(s.specular.r);
                AI_NFF_PARSE_FLOAT(d); // skip shininess and transmittance
                AI_NFF_PARSE_FLOAT(d);
                AI_NFF_PARSE_FLOAT(s.refracti);

                // NFF2 uses full colors here so we need to use them too
                // although NFF uses simple scaling factors
                s.diffuse.g = s.diffuse.b = s.diffuse.r;
                s.specular.g = s.specular.b = s.specular.r;

                // if the next one is NOT a number we assume it is a texture file name
