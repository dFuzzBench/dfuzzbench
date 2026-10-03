/*
Open Asset Import Library (assimp)
----------------------------------------------------------------------

Copyright (c) 2006-2026, assimp team
Copyright (c) 2019 bzt

All rights reserved.

Redistribution and use of this software in source and binary forms,
with or without modification, are permitted provided that the
following conditions are met:

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

----------------------------------------------------------------------
*/

#ifndef ASSIMP_BUILD_NO_M3D_IMPORTER

#define M3D_IMPLEMENTATION
#define M3D_NONORMALS /* leave the post-processing to Assimp */
#define M3D_NOWEIGHTS
#define M3D_NOANIMATION

#include <assimp/DefaultIOSystem.h>
#include <assimp/IOStreamBuffer.h>
#include <assimp/ai_assert.h>
#include <assimp/importerdesc.h>
#include <assimp/scene.h>
#include <assimp/DefaultLogger.hpp>
#include <assimp/Importer.hpp>
#include <memory>

#include "M3DImporter.h"
#include "M3DMaterials.h"
#include "M3DWrapper.h"

// RESOURCES:
// https://gitlab.com/bztsrc/model3d/blob/master/docs/m3d_format.md
// https://gitlab.com/bztsrc/model3d/blob/master/docs/a3d_format.md

/*
 Unfortunately aiNode has bone structures and meshes too, yet we can't assign
 the mesh to a bone aiNode as a skin may refer to several aiNodes. Therefore
 I've decided to import into this structure:

   aiScene->mRootNode
    |        |->mMeshes (all the meshes)
    |        \->children (empty if there's no skeleton imported, no meshes)
    |             \->skeleton root aiNode*
    |                   |->bone aiNode
    |                   |   \->subbone aiNode
    |                   |->bone aiNode
    |                   |   ...
    |                   \->bone aiNode
    \->mMeshes[]
        \->aiBone, referencing mesh-less aiNodes from above

  * - normally one, but if a model has several skeleton roots, then all of them
      are listed in aiScene->mRootNode->children, but all without meshes
*/

static constexpr aiImporterDesc desc = {
    "Model 3D Importer",
    "",
    "",
    "",
    aiImporterFlags_SupportTextFlavour | aiImporterFlags_SupportBinaryFlavour,
    0,
    0,
    0,
    0,
    "m3d a3d"
};

namespace Assimp {

using namespace std;

// ------------------------------------------------------------------------------------------------
//  Default constructor
M3DImporter::M3DImporter() :
        mScene(nullptr) {
    // empty
}

// ------------------------------------------------------------------------------------------------
//  Returns true, if file is a binary or ASCII Model 3D file.
bool M3DImporter::CanRead(const std::string &pFile, IOSystem *pIOHandler, bool /*checkSig*/) const {
    // don't use CheckMagicToken because that checks with swapped bytes too, leading to false
    // positives. This magic is not uint32_t, but char[4], so memcmp is the best way
    std::unique_ptr<IOStream> pStream(pIOHandler->Open(pFile, "rb"));
    unsigned char data[4];
    if (4 != pStream->Read(data, 1, 4)) {
        return false;
    }
    return !memcmp(data, "3DMO", 4) /* bin */
#ifdef M3D_ASCII
        || !memcmp(data, "3dmo", 4) /* ASCII */
#endif
            ;
}

// ------------------------------------------------------------------------------------------------
const aiImporterDesc *M3DImporter::GetInfo() const {
    return &desc;
}

// ------------------------------------------------------------------------------------------------
//  Model 3D import implementation
void M3DImporter::InternReadFile(const std::string &file, aiScene *pScene, IOSystem *pIOHandler) {
    // Read file into memory
    std::unique_ptr<IOStream> pStream(pIOHandler->Open(file, "rb"));
    if (!pStream.get()) {
        throw DeadlyImportError("Failed to open file ", file, ".");
    }

    // Get the file-size and validate it, throwing an exception when fails
    size_t fileSize = pStream->FileSize();
    if (fileSize < 8) {
        throw DeadlyImportError("M3D-file ", file, " is too small.");
    }
    std::vector<unsigned char> buffer(fileSize);
    if (fileSize != pStream->Read(buffer.data(), 1, fileSize)) {
        throw DeadlyImportError("Failed to read the file ", file, ".");
    }
    // extra check for binary format's first 8 bytes. Not done for the ASCII variant
    if (!memcmp(buffer.data(), "3DMO", 4) && memcmp(buffer.data() + 4, &fileSize, 4)) {
        throw DeadlyImportError("Bad binary header in file ", file, ".");
    }
    // make sure there's a terminator zero character, as input must be ASCIIZ
    if (!memcmp(buffer.data(), "3dmo", 4)) {
        buffer.push_back(0);
    }

    // Get the path for external assets
    std::string folderName("./");
    std::string::size_type pos = file.find_last_of("\\/");
    if (pos != std::string::npos) {
        folderName = file.substr(0, pos);
        if (!folderName.empty()) {
            pIOHandler->PushDirectory(folderName);
        }
    }

    //DefaultLogger::create("/dev/stderr", Logger::VERBOSE);
    ASSIMP_LOG_DEBUG("M3D: loading ", file);

    // let the C SDK do the hard work for us
    M3DWrapper m3d(pIOHandler, buffer);

    if (!m3d) {
        throw DeadlyImportError("Unable to parse ", file, " as M3D.");
    }

    // create the root node
    pScene->mRootNode = new aiNode;
    pScene->mRootNode->mName = aiString(m3d.Name());
    pScene->mRootNode->mTransformation = aiMatrix4x4();
    pScene->mRootNode->mNumChildren = 0;
    mScene = pScene;

    ASSIMP_LOG_DEBUG("M3D: root node ", m3d.Name());

    // now we just have to fill up the Assimp structures in pScene
    importMaterials(m3d);
    importTextures(m3d);
    importBones(m3d, M3D_NOTDEFINED, pScene->mRootNode);
    importMeshes(m3d);
    importAnimations(m3d);

    // Pop directory stack
    if (pIOHandler->StackSize() > 0) {
        pIOHandler->PopDirectory();
    }
}

// ------------------------------------------------------------------------------------------------
// convert materials. properties are converted using a static table in M3DMaterials.h
void M3DImporter::importMaterials(const M3DWrapper &m3d) {
    unsigned int i, j, k, l, n;
    m3dm_t *m;
    aiString name = aiString(AI_DEFAULT_MATERIAL_NAME);
    aiColor4D c;
    ai_real f;

    ai_assert(mScene != nullptr);
    ai_assert(m3d);

    mScene->mNumMaterials = m3d->nummaterial + 1;
    mScene->mMaterials = new aiMaterial *[mScene->mNumMaterials];

    ASSIMP_LOG_DEBUG("M3D: importMaterials ", mScene->mNumMaterials);

    // add a default material as first
    aiMaterial *defaultMat = new aiMaterial;
    defaultMat->AddProperty(&name, AI_MATKEY_NAME);
    c.a = 1.0f;
    c.b = c.g = c.r = 0.6f;
    defaultMat->AddProperty(&c, 1, AI_MATKEY_COLOR_DIFFUSE);
    mScene->mMaterials[0] = defaultMat;

    if (!m3d->nummaterial || !m3d->material) {
        return;
    }

    for (i = 0; i < m3d->nummaterial; i++) {
