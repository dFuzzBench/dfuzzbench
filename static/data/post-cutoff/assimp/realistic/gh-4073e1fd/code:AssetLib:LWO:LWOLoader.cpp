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

/** @file  LWOLoader.cpp
 *  @brief Implementation of the LWO importer class
 */

#ifndef ASSIMP_BUILD_NO_LWO_IMPORTER

// internal headers
#include "LWOLoader.h"
#include "PostProcessing/ConvertToLHProcess.h"
#include "PostProcessing/ProcessHelper.h"
#include "Geometry/GeometryUtils.h"

#include <assimp/ByteSwapper.h>
#include <assimp/SGSpatialSort.h>
#include <assimp/StringComparison.h>
#include <assimp/importerdesc.h>
#include <assimp/IOSystem.hpp>

#include <iomanip>
#include <map>
#include <memory>
#include <sstream>

using namespace Assimp;

static constexpr aiImporterDesc desc = {
    "LightWave/Modo Object Importer",
    "",
    "",
    "https://www.lightwave3d.com/lightwave_sdk/",
    aiImporterFlags_SupportTextFlavour,
    0,
    0,
    0,
    0,
    "lwo lxo"
};

// ------------------------------------------------------------------------------------------------
// Returns whether the class can handle the format of the given file.
bool LWOImporter::CanRead(const std::string &file, IOSystem *pIOHandler, bool /*checkSig*/) const {
    static constexpr uint32_t tokens[] = {
        AI_LWO_FOURCC_LWOB,
        AI_LWO_FOURCC_LWO2,
        AI_LWO_FOURCC_LXOB
    };
    return CheckMagicToken(pIOHandler, file, tokens, AI_COUNT_OF(tokens), 8);
}

// ------------------------------------------------------------------------------------------------
// Setup configuration properties
void LWOImporter::SetupProperties(const Importer *pImp) {
    configSpeedFlag = (0 != pImp->GetPropertyInteger(AI_CONFIG_FAVOUR_SPEED, 0) ? true : false);
    configLayerIndex = pImp->GetPropertyInteger(AI_CONFIG_IMPORT_LWO_ONE_LAYER_ONLY, UINT_MAX);
    configLayerName = pImp->GetPropertyString(AI_CONFIG_IMPORT_LWO_ONE_LAYER_ONLY, "");
}

// ------------------------------------------------------------------------------------------------
// Get list of file extensions
const aiImporterDesc *LWOImporter::GetInfo() const {
    return &desc;
}

// ------------------------------------------------------------------------------------------------
// Imports the given file into the given scene structure.
void LWOImporter::InternReadFile(const std::string &pFile,
        aiScene *pScene,
        IOSystem *pIOHandler) {
    std::unique_ptr<IOStream> file(pIOHandler->Open(pFile, "rb"));

    // Check whether we can read from the file
    if (file == nullptr) {
        throw DeadlyImportError("Failed to open LWO file ", pFile, ".");
    }

    if ((this->fileSize = (unsigned int)file->FileSize()) < 12) {
        throw DeadlyImportError("LWO: The file is too small to contain the IFF header");
    }

    // Allocate storage and copy the contents of the file to a memory buffer
    std::vector<uint8_t> mBuffer(fileSize);
    file->Read(&mBuffer[0], 1, fileSize);
    mScene = pScene;

    // Determine the type of the file
    uint32_t fileType;
    const char *sz = IFF::ReadHeader(&mBuffer[0], fileType);
    if (sz) {
        throw DeadlyImportError(sz);
    }

    mFileBuffer = &mBuffer[0] + 12;
    mFileBufferEnd = &mBuffer[0] + fileSize;
    fileSize -= 12;

    // Initialize some members with their default values
    hasNamedLayer = false;

    // Create temporary storage on the stack but store pointers to it in the class
    // instance. Therefore everything will be destructed properly if an exception
    // is thrown and we needn't take care of that.
    LayerList _mLayers;
    SurfaceList _mSurfaces;
    TagList _mTags;
    TagMappingTable _mMapping;

    mLayers = &_mLayers;
    mTags = &_mTags;
    mMapping = &_mMapping;
    mSurfaces = &_mSurfaces;

    // Allocate a default layer (layer indices are 1-based from now)
    mLayers->push_back(Layer());
    mCurLayer = &mLayers->back();
    mCurLayer->mName = "<LWODefault>";
    mCurLayer->mIndex = 1;

    // old lightwave file format (prior to v6)
    mIsLWO2 = false;
    mIsLWO3 = false;
    mIsLXOB = false;

    if (AI_LWO_FOURCC_LWOB == fileType) {
        ASSIMP_LOG_INFO("LWO file format: LWOB (<= LightWave 5.5)");

        LoadLWOBFile();
    } else if (AI_LWO_FOURCC_LWO2 == fileType) {
        // New lightwave format
        ASSIMP_LOG_INFO("LWO file format: LWO2 (>= LightWave 6)");
    } else if ( AI_LWO_FOURCC_LWO3 == fileType ) {
        ASSIMP_LOG_INFO("LWO file format: LWO3 (>= LightWave 2018)");
    } else if (AI_LWO_FOURCC_LXOB == fileType) {
        // MODO file format
        mIsLXOB = true;
        ASSIMP_LOG_INFO("LWO file format: LXOB (Modo)");
    }
    else {
        char szBuff[5];
        szBuff[0] = (char)(fileType >> 24u);
        szBuff[1] = (char)(fileType >> 16u);
        szBuff[2] = (char)(fileType >> 8u);
        szBuff[3] = (char)(fileType);
        szBuff[4] = '\0';
        throw DeadlyImportError("Unknown LWO sub format: ", szBuff);
    }

    if (AI_LWO_FOURCC_LWOB != fileType) {   //
        if( AI_LWO_FOURCC_LWO3 == fileType ) {
            mIsLWO3 = true;
        } else {
            mIsLWO2 = true;
        }

        LoadLWO2File();

        // The newer lightwave format allows the user to configure the
        // loader that just one layer is used. If this is the case
        // we need to check now whether the requested layer has been found.
        if (UINT_MAX != configLayerIndex) {
            unsigned int layerCount = 0;
            for (std::list<LWO::Layer>::iterator itLayers = mLayers->begin(); itLayers != mLayers->end(); ++itLayers)
                if (!itLayers->skip)
                    layerCount++;
            if (layerCount != 2)
                throw DeadlyImportError("LWO2: The requested layer was not found");
        }

        if (configLayerName.length() && !hasNamedLayer) {
            throw DeadlyImportError("LWO2: Unable to find the requested layer: ", configLayerName);
        }
    }

    // now, as we have loaded all data, we can resolve cross-referenced tags and clips
    ResolveTags();
    ResolveClips();

    // now process all layers and build meshes and nodes
    std::vector<aiMesh *> apcMeshes;
    std::map<uint16_t, aiNode *> apcNodes;

    apcMeshes.reserve(mLayers->size() * std::min(((unsigned int)mSurfaces->size() / 2u), 1u));

    unsigned int iDefaultSurface = UINT_MAX; // index of the default surface
    for (LWO::Layer &layer : *mLayers) {
        if (layer.skip)
            continue;

        // I don't know whether there could be dummy layers, but it would be possible
        const unsigned int meshStart = (unsigned int)apcMeshes.size();
        if (!layer.mFaces.empty() && !layer.mTempPoints.empty()) {

            // now sort all faces by the surfaces assigned to them
            std::vector<SortedRep> pSorted(mSurfaces->size() + 1);

            unsigned int i = 0;
            for (FaceList::iterator it = layer.mFaces.begin(), end = layer.mFaces.end(); it != end; ++it, ++i) {
                // Check whether we support this face's type
                if ((*it).type != AI_LWO_FACE && (*it).type != AI_LWO_PTCH &&
                        (*it).type != AI_LWO_BONE && (*it).type != AI_LWO_SUBD) {
                    continue;
                }

                unsigned int idx = (*it).surfaceIndex;
                if (idx >= mTags->size()) {
                    ASSIMP_LOG_WARN("LWO: Invalid face surface index");
                    idx = UINT_MAX;
                }
                if (UINT_MAX == idx || UINT_MAX == (idx = _mMapping[idx])) {
                    if (UINT_MAX == iDefaultSurface) {
                        iDefaultSurface = (unsigned int)mSurfaces->size();
                        mSurfaces->push_back(LWO::Surface());
                        LWO::Surface &surf = mSurfaces->back();
                        surf.mColor.r = surf.mColor.g = surf.mColor.b = 0.6f;
                        surf.mName = "LWODefaultSurface";
                    }
                    idx = iDefaultSurface;
                }
                pSorted[idx].push_back(i);
            }
            if (UINT_MAX == iDefaultSurface) {
                pSorted.erase(pSorted.end() - 1);
            }
            for (unsigned int j = 0; j < mSurfaces->size(); ++j) {
                SortedRep &sorted = pSorted[j];
                if (sorted.empty())
                    continue;

                // generate the mesh
                aiMesh *mesh = new aiMesh();
                apcMeshes.push_back(mesh);
                mesh->mNumFaces = (unsigned int)sorted.size();

                // count the number of vertices
                SortedRep::const_iterator it = sorted.begin(), end = sorted.end();
                for (; it != end; ++it) {
                    mesh->mNumVertices += layer.mFaces[*it].mNumIndices;
                }

                aiVector3D *nrm = nullptr, *pv = mesh->mVertices = new aiVector3D[mesh->mNumVertices];
                aiFace *pf = mesh->mFaces = new aiFace[mesh->mNumFaces];
                mesh->mMaterialIndex = j;

                // find out which vertex color channels and which texture coordinate
                // channels are really required by the material attached to this mesh
                unsigned int vUVChannelIndices[AI_MAX_NUMBER_OF_TEXTURECOORDS];
                unsigned int vVColorIndices[AI_MAX_NUMBER_OF_COLOR_SETS];

#ifdef ASSIMP_BUILD_DEBUG
                for (unsigned int mui = 0; mui < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++mui) {
                    vUVChannelIndices[mui] = UINT_MAX;
                }
                for (unsigned int mui = 0; mui < AI_MAX_NUMBER_OF_COLOR_SETS; ++mui) {
                    vVColorIndices[mui] = UINT_MAX;
                }
#endif

                FindUVChannels(_mSurfaces[j], sorted, layer, vUVChannelIndices);
                FindVCChannels(_mSurfaces[j], sorted, layer, vVColorIndices);

                // allocate storage for UV and CV channels
                aiVector3D *pvUV[AI_MAX_NUMBER_OF_TEXTURECOORDS];
                for (unsigned int mui = 0; mui < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++mui) {
                    if (UINT_MAX == vUVChannelIndices[mui]) {
                        break;
                    }

                    pvUV[mui] = mesh->mTextureCoords[mui] = new aiVector3D[mesh->mNumVertices];

                    // LightWave doesn't support more than 2 UV components (?)
                    mesh->mNumUVComponents[0] = 2;
                }

                if (layer.mNormals.name.length()) {
                    nrm = mesh->mNormals = new aiVector3D[mesh->mNumVertices];
                }

                aiColor4D *pvVC[AI_MAX_NUMBER_OF_COLOR_SETS];
                for (unsigned int mui = 0; mui < AI_MAX_NUMBER_OF_COLOR_SETS; ++mui) {
                    if (UINT_MAX == vVColorIndices[mui]) {
                        break;
                    }
                    pvVC[mui] = mesh->mColors[mui] = new aiColor4D[mesh->mNumVertices];
                }

                // we would not need this extra array, but the code is much cleaner if we use it
                std::vector<unsigned int> &smoothingGroups = layer.mPointReferrers;
                smoothingGroups.erase(smoothingGroups.begin(), smoothingGroups.end());
                smoothingGroups.resize(mesh->mNumFaces, 0);

                // now convert all faces
                unsigned int vert = 0;
                std::vector<unsigned int>::iterator outIt = smoothingGroups.begin();
                for (it = sorted.begin(); it != end; ++it, ++outIt) {
                    const LWO::Face &face = layer.mFaces[*it];
                    *outIt = face.smoothGroup;

                    // copy all vertices
                    for (unsigned int q = 0; q < face.mNumIndices; ++q, ++vert) {
                        unsigned int idx = face.mIndices[q];
                        *pv++ = layer.mTempPoints[idx] /*- layer.mPivot*/;

                        // process UV coordinates
                        for (unsigned int w = 0; w < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++w) {
                            if (UINT_MAX == vUVChannelIndices[w]) {
                                break;
                            }
                            aiVector3D *&pp = pvUV[w];
                            const aiVector2D &src = ((aiVector2D *)&layer.mUVChannels[vUVChannelIndices[w]].rawData[0])[idx];
                            pp->x = src.x;
                            pp->y = src.y;
                            pp++;
                        }

                        // process normals (MODO extension)
                        if (nrm) {
                            *nrm = ((aiVector3D *)&layer.mNormals.rawData[0])[idx];
                            nrm->z *= -1.f;
                            ++nrm;
                        }

                        // process vertex colors
                        for (unsigned int w = 0; w < AI_MAX_NUMBER_OF_COLOR_SETS; ++w) {
                            if (UINT_MAX == vVColorIndices[w]) {
                                break;
                            }
                            *pvVC[w] = ((aiColor4D *)&layer.mVColorChannels[vVColorIndices[w]].rawData[0])[idx];

                            // If a RGB color map is explicitly requested delete the
                            // alpha channel - it could theoretically be != 1.
                            if (_mSurfaces[j].mVCMapType == AI_LWO_RGB)
                                pvVC[w]->a = 1.f;

                            pvVC[w]++;
                        }

                        face.mIndices[q] = vert;
                    }
                    pf->mIndices = face.mIndices;
                    pf->mNumIndices = face.mNumIndices;
                    unsigned int **facePtr = (unsigned int **)&face.mIndices;
                    *facePtr = nullptr; // HACK: make sure it won't be deleted
                    pf++;
                }

                if (!mesh->mNormals) {
                    // Compute normal vectors for the mesh - we can't use our GenSmoothNormal-
                    // Step here since it wouldn't handle smoothing groups correctly for LWO.
                    // So we use a separate implementation.
                    ComputeNormals(mesh, smoothingGroups, _mSurfaces[j]);
                } else {
                    ASSIMP_LOG_VERBOSE_DEBUG("LWO2: No need to compute normals, they're already there");
                }
            }
        }

        // Generate nodes to render the mesh. Store the source layer in the mParent member of the nodes
        unsigned int num = static_cast<unsigned int>(apcMeshes.size() - meshStart);
        if (layer.mName != "<LWODefault>" || num > 0) {
            std::unique_ptr<aiNode> pcNode(new aiNode());
            pcNode->mName.Set(layer.mName);
            pcNode->mParent = (aiNode *)&layer;
            pcNode->mNumMeshes = num;

            if (pcNode->mNumMeshes) {
                pcNode->mMeshes = new unsigned int[pcNode->mNumMeshes];
                for (unsigned int p = 0; p < pcNode->mNumMeshes; ++p)
                    pcNode->mMeshes[p] = p + meshStart;
            }
            ASSIMP_LOG_DEBUG("insert apcNode for layer ", layer.mIndex, " \"", layer.mName, "\"");
            apcNodes[layer.mIndex] = pcNode.release();
        }
    }

    if (apcNodes.empty() || apcMeshes.empty())
        throw DeadlyImportError("LWO: No meshes loaded");

    // The RemoveRedundantMaterials step will clean this up later
    pScene->mMaterials = new aiMaterial *[pScene->mNumMaterials = (unsigned int)mSurfaces->size()];

    for (unsigned int mat = 0; mat < pScene->mNumMaterials; ++mat) {
        aiMaterial *pcMat = new aiMaterial();
        pScene->mMaterials[mat] = pcMat;
        ConvertMaterial((*mSurfaces)[mat], pcMat);
    }

    // copy the meshes to the output structure
    pScene->mMeshes = new aiMesh *[pScene->mNumMeshes = (unsigned int)apcMeshes.size()];
    ::memcpy(pScene->mMeshes, &apcMeshes[0], pScene->mNumMeshes * sizeof(void *));

    // generate the final node graph
    GenerateNodeGraph(apcNodes);
}

// ------------------------------------------------------------------------------------------------
void LWOImporter::ComputeNormals(aiMesh *mesh, const std::vector<unsigned int> &smoothingGroups,
        const LWO::Surface &surface) {
    // Allocate output storage
    mesh->mNormals = new aiVector3D[mesh->mNumVertices];

    // First generate per-face normals
    aiVector3D *out;
    std::vector<aiVector3D> faceNormals;

    // ... in some cases that's already enough
    if (!surface.mMaximumSmoothAngle)
        out = mesh->mNormals;
    else {
        faceNormals.resize(mesh->mNumVertices);
        out = &faceNormals[0];
    }

    aiFace *begin = mesh->mFaces, *const end = mesh->mFaces + mesh->mNumFaces;
    for (; begin != end; ++begin) {
        aiFace &face = *begin;

        if (face.mNumIndices < 3) {
            continue;
        }

        // LWO doc: "the normal is defined as the cross product of the first and last edges"
        aiVector3D *pV1 = mesh->mVertices + face.mIndices[0];
        aiVector3D *pV2 = mesh->mVertices + face.mIndices[1];
        aiVector3D *pV3 = mesh->mVertices + face.mIndices[face.mNumIndices - 1];

        aiVector3D vNor = ((*pV2 - *pV1) ^ (*pV3 - *pV1)).Normalize();
        for (unsigned int i = 0; i < face.mNumIndices; ++i)
            out[face.mIndices[i]] = vNor;
    }
    if (!surface.mMaximumSmoothAngle) return;
    const float posEpsilon = ComputePositionEpsilon(mesh);

    // Now generate the spatial sort tree
    SGSpatialSort sSort;
    std::vector<unsigned int>::const_iterator it = smoothingGroups.begin();
    for (begin = mesh->mFaces; begin != end; ++begin, ++it) {
        aiFace &face = *begin;
        for (unsigned int i = 0; i < face.mNumIndices; ++i) {
            unsigned int tt = face.mIndices[i];
            sSort.Add(mesh->mVertices[tt], tt, *it);
        }
    }
    // Sort everything - this takes O(nlogn) time
    sSort.Prepare();
    std::vector<unsigned int> poResult;
    poResult.reserve(20);

    // Generate vertex normals. We have O(logn) for the binary lookup, which we need
    // for n elements, thus the EXPECTED complexity is O(nlogn)
    if (surface.mMaximumSmoothAngle < 3.f && !configSpeedFlag) {
        const float fLimit = std::cos(surface.mMaximumSmoothAngle);

        for (begin = mesh->mFaces, it = smoothingGroups.begin(); begin != end; ++begin, ++it) {
            const aiFace &face = *begin;
            unsigned int *beginIdx = face.mIndices, *const endIdx = face.mIndices + face.mNumIndices;
            for (; beginIdx != endIdx; ++beginIdx) {
                unsigned int idx = *beginIdx;
                sSort.FindPositions(mesh->mVertices[idx], *it, posEpsilon, poResult, true);

                aiVector3D vNormals;
                 for (std::vector<unsigned int>::const_iterator a = poResult.begin(); a != poResult.end(); ++a) {
                    const aiVector3D &v = faceNormals[*a];
                    if (v * faceNormals[idx] < fLimit)
                        continue;
                    vNormals += v;
                }
                mesh->mNormals[idx] = vNormals.Normalize();
            }
        }
    }
    // faster code path in case there is no smooth angle
    else {
        std::vector<bool> vertexDone(mesh->mNumVertices, false);
        for (begin = mesh->mFaces, it = smoothingGroups.begin(); begin != end; ++begin, ++it) {
            const aiFace &face = *begin;
            unsigned int *beginIdx = face.mIndices, *const endIdx = face.mIndices + face.mNumIndices;
            for (; beginIdx != endIdx; ++beginIdx) {
                unsigned int idx = *beginIdx;
                if (vertexDone[idx])
                    continue;
                sSort.FindPositions(mesh->mVertices[idx], *it, posEpsilon, poResult, true);

                aiVector3D vNormals;
                 for (std::vector<unsigned int>::const_iterator a = poResult.begin(); a != poResult.end(); ++a) {
                    const aiVector3D &v = faceNormals[*a];
                    vNormals += v;
                }
                vNormals.Normalize();
                for (std::vector<unsigned int>::const_iterator a = poResult.begin(); a != poResult.end(); ++a) {
                    mesh->mNormals[*a] = vNormals;
                    vertexDone[*a] = true;
                }
            }
        }
    }
    GeometryUtils::normalizeVectorArray(mesh->mNormals, mesh->mNormals, mesh->mNumVertices);
}

// ------------------------------------------------------------------------------------------------
void LWOImporter::GenerateNodeGraph(std::map<uint16_t, aiNode *> &apcNodes) {
    // now generate the final nodegraph - generate a root node and attach children
    aiNode *root = mScene->mRootNode = new aiNode();
    root->mName.Set("<LWORoot>");

    ASSIMP_LOG_DEBUG("apcNodes initial size: ", apcNodes.size());
    if (!apcNodes.empty()) {
        ASSIMP_LOG_DEBUG("first apcNode is: ", apcNodes.begin()->first, " \"", apcNodes.begin()->second->mName.C_Str(), "\"");
    }

    //Set parent of all children, inserting pivots
    {
        std::map<uint16_t, aiNode *> mapPivot;
        for (auto itapcNodes = apcNodes.begin(); itapcNodes != apcNodes.end(); ++itapcNodes) {

            //Get the parent index
            LWO::Layer *nodeLayer = (LWO::Layer *)(itapcNodes->second->mParent);
            uint16_t parentIndex = nodeLayer->mParent;

            //Create pivot node, store it into the pivot map, and set the parent as the pivot
            std::unique_ptr<aiNode> pivotNode(new aiNode());
            pivotNode->mName.Set("Pivot-" + std::string(itapcNodes->second->mName.data));
            itapcNodes->second->mParent = pivotNode.get();

            //Look for the parent node to attach the pivot to
            if (apcNodes.find(parentIndex) != apcNodes.end()) {
                pivotNode->mParent = apcNodes[parentIndex];
            } else {
                //If not, attach to the root node
                pivotNode->mParent = root;
            }

            //Set the node and the pivot node transformation
            itapcNodes->second->mTransformation.a4 = -nodeLayer->mPivot.x;
            itapcNodes->second->mTransformation.b4 = -nodeLayer->mPivot.y;
            itapcNodes->second->mTransformation.c4 = -nodeLayer->mPivot.z;
            pivotNode->mTransformation.a4 = nodeLayer->mPivot.x;
            pivotNode->mTransformation.b4 = nodeLayer->mPivot.y;
            pivotNode->mTransformation.c4 = nodeLayer->mPivot.z;
            uint16_t pivotNodeId = static_cast<uint16_t>(-(itapcNodes->first + 2));
            ASSIMP_LOG_DEBUG("insert pivot node: ", pivotNodeId);
            auto oldNodeIt = mapPivot.find(pivotNodeId);
            if (oldNodeIt != mapPivot.end()) {
                ASSIMP_LOG_ERROR("attempted to insert pivot node which already exists in pivot map ", pivotNodeId, " \"", pivotNode->mName.C_Str(), "\"");
            } else {
                mapPivot.emplace(pivotNodeId, pivotNode.release());
            }
        }

        ASSIMP_LOG_DEBUG("pivot nodes: ", mapPivot.size());
        //Merge pivot map into node map
        for (auto itMapPivot = mapPivot.begin(); itMapPivot != mapPivot.end();) {
            uint16_t pivotNodeId = itMapPivot->first;
            auto oldApcNodeIt = apcNodes.find(pivotNodeId);
            if (oldApcNodeIt != apcNodes.end()) {
                ASSIMP_LOG_ERROR("attempted to insert pivot node which already exists in apc nodes ", pivotNodeId, " \"", itMapPivot->second->mName.C_Str(), "\"");
            } else {
                apcNodes.emplace(pivotNodeId, itMapPivot->second);
            }
            itMapPivot->second = nullptr;
            itMapPivot = mapPivot.erase(itMapPivot);
        }
        ASSIMP_LOG_DEBUG("total nodes: ", apcNodes.size());
    }

    //Set children of all parents
    apcNodes[(uint16_t)-1] = root;
    for (auto itMapParentNodes = apcNodes.begin(); itMapParentNodes != apcNodes.end(); ++itMapParentNodes) {
        for (auto itMapChildNodes = apcNodes.begin(); itMapChildNodes != apcNodes.end(); ++itMapChildNodes) {
            if ((itMapParentNodes->first != itMapChildNodes->first) && (itMapParentNodes->second == itMapChildNodes->second->mParent)) {
                ++(itMapParentNodes->second->mNumChildren);
            }
        }
        if (itMapParentNodes->second->mNumChildren) {
            itMapParentNodes->second->mChildren = new aiNode *[itMapParentNodes->second->mNumChildren];
            uint16_t p = 0;
            for (auto itMapChildNodes = apcNodes.begin(); itMapChildNodes != apcNodes.end(); ++itMapChildNodes) {
                if ((itMapParentNodes->first != itMapChildNodes->first) && (itMapParentNodes->second == itMapChildNodes->second->mParent)) {
                    itMapParentNodes->second->mChildren[p++] = itMapChildNodes->second;
                }
            }
        }
    }

    if (!mScene->mRootNode->mNumChildren) {
        ASSIMP_LOG_DEBUG("All apcNodes:");
        for (auto nodeIt = apcNodes.begin(); nodeIt != apcNodes.end(); ) {
            ASSIMP_LOG_DEBUG("Node ", nodeIt->first, " \"", nodeIt->second->mName.C_Str(), "\"");
            nodeIt->second = nullptr;
            nodeIt = apcNodes.erase(nodeIt);
        }
        throw DeadlyImportError("LWO: Unable to build a valid node graph");
    }

    // Remove a single root node with no meshes assigned to it ...
    if (1 == mScene->mRootNode->mNumChildren) {
        aiNode *pc = mScene->mRootNode->mChildren[0];
        pc->mParent = mScene->mRootNode->mChildren[0] = nullptr;
        delete mScene->mRootNode;
        mScene->mRootNode = pc;
    }

    // convert the whole stuff to RH with CCW winding
    MakeLeftHandedProcess maker;
    maker.Execute(mScene);

    FlipWindingOrderProcess flipper;
    flipper.Execute(mScene);
}

// ------------------------------------------------------------------------------------------------
void LWOImporter::ResolveTags() {
    // --- this function is used for both LWO2 and LWOB
    mMapping->resize(mTags->size(), UINT_MAX);
    for (unsigned int a = 0; a < mTags->size(); ++a) {

        const std::string &c = (*mTags)[a];
        for (unsigned int i = 0; i < mSurfaces->size(); ++i) {

            const std::string &d = (*mSurfaces)[i].mName;
            if (!ASSIMP_stricmp(c, d)) {

                (*mMapping)[a] = i;
                break;
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------
void LWOImporter::ResolveClips() {
    for (unsigned int i = 0; i < mClips.size(); ++i) {

        Clip &clip = mClips[i];
        if (Clip::REF == clip.type) {

            if (clip.clipRef >= mClips.size()) {
                ASSIMP_LOG_ERROR("LWO2: Clip referrer index is out of range");
                clip.clipRef = 0;
            }

            Clip &dest = mClips[clip.clipRef];
            if (Clip::REF == dest.type) {
                ASSIMP_LOG_ERROR("LWO2: Clip references another clip reference");
                clip.type = Clip::UNSUPPORTED;
            }

            else {
                clip.path = dest.path;
                clip.type = dest.type;
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------
void LWOImporter::AdjustTexturePath(std::string &out) {
    // --- this function is used for both LWO2 and LWOB
    if (!mIsLWO2 && !mIsLWO3 && ::strstr(out.c_str(), "(sequence)")) {

        // remove the (sequence) and append 000
        ASSIMP_LOG_INFO("LWOB: Sequence of animated texture found. It will be ignored");
        out = out.substr(0, out.length() - 10) + "000";
    }

    // format: drive:path/file - we just need to insert a slash after the drive
    std::string::size_type n = out.find_first_of(':');
    if (std::string::npos != n) {
        out.insert(n + 1, "/");
    }
}

// ------------------------------------------------------------------------------------------------
void LWOImporter::LoadLWOTags(unsigned int size) {
    // --- this function is used for both LWO2 and LWOB

    const char *szCur = (const char *)mFileBuffer, *szLast = szCur;
    const char *const szEnd = szLast + size;
    while (szCur < szEnd) {
        if (!(*szCur)) {
            const size_t len = (size_t)(szCur - szLast);
            // FIX: skip empty-sized tags
            if (len)
                mTags->push_back(std::string(szLast, len));
            szCur += (len & 0x1 ? 1 : 2);
            szLast = szCur;
        }
        szCur++;
    }
}

// ------------------------------------------------------------------------------------------------
void LWOImporter::LoadLWOPoints(unsigned int length) {
    // --- this function is used for both LWO2 and LWOB but for
    // LWO2 we need to allocate 25% more storage - it could be we'll
    // need to duplicate some points later.
    const size_t vertexLen = 12;
    if ((length % vertexLen) != 0) {
        throw DeadlyImportError("LWO2: Points chunk length is not multiple of vertexLen (12)");
    }
    unsigned int regularSize = (unsigned int)mCurLayer->mTempPoints.size() + length / 12;
    if (mIsLWO2 || mIsLWO3) {
        mCurLayer->mTempPoints.reserve(regularSize + (regularSize >> 2u));
        mCurLayer->mTempPoints.resize(regularSize);

        // initialize all point referrers with the default values
        mCurLayer->mPointReferrers.reserve(regularSize + (regularSize >> 2u));
        mCurLayer->mPointReferrers.resize(regularSize, UINT_MAX);
    } else
        mCurLayer->mTempPoints.resize(regularSize);

        // perform endianness conversions
#ifndef AI_BUILD_BIG_ENDIAN
    for (unsigned int i = 0; i<length >> 2; ++i)
        ByteSwap::Swap4(mFileBuffer + (i << 2));
#endif
    ::memcpy(&mCurLayer->mTempPoints[0], mFileBuffer, length);
}

// ------------------------------------------------------------------------------------------------
void LWOImporter::LoadLWO2Polygons(unsigned int length) {
    LE_NCONST uint16_t *const end = (LE_NCONST uint16_t *)(mFileBuffer + length);
    const uint32_t type = GetU4();

    // Determine the type of the polygons
    switch (type) {
            // read unsupported stuff too (although we won't process it)
        case AI_LWO_MBAL:
            ASSIMP_LOG_WARN("LWO2: Encountered unsupported primitive chunk (METABALL)");
            break;
        case AI_LWO_CURV:
            ASSIMP_LOG_WARN("LWO2: Encountered unsupported primitive chunk (SPLINE)");
            ;
            break;

            // These are ok with no restrictions
        case AI_LWO_PTCH:
        case AI_LWO_FACE:
        case AI_LWO_BONE:
        case AI_LWO_SUBD:
            break;
        default:

            // hm!? wtf is this? ok ...
            ASSIMP_LOG_ERROR("LWO2: Ignoring unknown polygon type.");
            break;
    }

    // first find out how many faces and vertices we'll finally need
    uint16_t *cursor = (uint16_t *)mFileBuffer;

    unsigned int iNumFaces = 0, iNumVertices = 0;
    CountVertsAndFacesLWO2(iNumVertices, iNumFaces, cursor, end);

    // allocate the output array and copy face indices
    if (iNumFaces) {
        cursor = (uint16_t *)mFileBuffer;

        mCurLayer->mFaces.resize(iNumFaces, LWO::Face(type));
        FaceList::iterator it = mCurLayer->mFaces.begin();
        CopyFaceIndicesLWO2(it, cursor, end);
    }
}

// ------------------------------------------------------------------------------------------------
void LWOImporter::CountVertsAndFacesLWO2(unsigned int &verts, unsigned int &faces,
        uint16_t *&cursor, const uint16_t *const end, unsigned int max) {
    while (cursor < end && max--) {
        uint16_t numIndices;
        ::memcpy(&numIndices, cursor++, 2);
        AI_LSWAP2(numIndices);
        numIndices &= 0x03FF;

        verts += numIndices;
        ++faces;

        for (uint16_t i = 0; i < numIndices; i++) {
            ReadVSizedIntLWO2((uint8_t *&)cursor);
        }
    }
}

// ------------------------------------------------------------------------------------------------
void LWOImporter::CopyFaceIndicesLWO2(FaceList::iterator &it,
        uint16_t *&cursor,
        const uint16_t *const end) {
    while (cursor < end) {
        LWO::Face &face = *it++;
        uint16_t numIndices;
        ::memcpy(&numIndices, cursor++, 2);
        AI_LSWAP2(numIndices);
        face.mNumIndices = numIndices & 0x03FF;

        if (face.mNumIndices) /* byte swapping has already been done */
        {
            face.mIndices = new unsigned int[face.mNumIndices];
            for (unsigned int i = 0; i < face.mNumIndices; i++) {
                face.mIndices[i] = ReadVSizedIntLWO2((uint8_t *&)cursor) + mCurLayer->mPointIDXOfs;
                if (face.mIndices[i] >= mCurLayer->mTempPoints.size()) {
                    ASSIMP_LOG_WARN("LWO2: Failure evaluating face record, index is out of range");
                    face.mIndices[i] = (unsigned int)mCurLayer->mTempPoints.size() - 1;
                }
            }
        } else
            throw DeadlyImportError("LWO2: Encountered invalid face record with zero indices");
    }
}

// ------------------------------------------------------------------------------------------------
void LWOImporter::LoadLWO2PolygonTags(unsigned int length) {
    LE_NCONST uint8_t *const end = mFileBuffer + length;

    AI_LWO_VALIDATE_CHUNK_LENGTH(length, PTAG, 4);
    uint32_t type = GetU4();

    if (type != AI_LWO_SURF && type != AI_LWO_SMGP)
        return;

    while (mFileBuffer < end) {
        unsigned int i = ReadVSizedIntLWO2(mFileBuffer) + mCurLayer->mFaceIDXOfs;
        unsigned int j = GetU2();

        if (i >= mCurLayer->mFaces.size()) {
            ASSIMP_LOG_WARN("LWO2: face index in PTAG is out of range");
            continue;
        }

        switch (type) {

            case AI_LWO_SURF:
                mCurLayer->mFaces[i].surfaceIndex = j;
                break;
            case AI_LWO_SMGP: /* is that really used? */
                mCurLayer->mFaces[i].smoothGroup = j;
                break;
        };
    }
}

// ------------------------------------------------------------------------------------------------
template <class T>
VMapEntry *FindEntry(std::vector<T> &list, const std::string &name, bool perPoly) {
    for (auto &elem : list) {
        if (elem.name == name) {
            if (!perPoly) {
                ASSIMP_LOG_WARN("LWO2: Found two VMAP sections with equal names");
            }
            return &elem;
        }
    }
    list.push_back(T());
    VMapEntry *p = &list.back();
    p->name = name;
    return p;
}

// ------------------------------------------------------------------------------------------------
template <class T>
inline void CreateNewEntry(T &chan, unsigned int srcIdx) {
    if (!chan.name.length())
        return;

