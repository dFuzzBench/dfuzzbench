/*
Open Asset Import Library (assimp)
----------------------------------------------------------------------

Copyright (c) 2006-2022, assimp team


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
#ifndef ASSIMP_BUILD_NO_EXPORT
#ifndef ASSIMP_BUILD_NO_GLTF_EXPORTER

#include "AssetLib/glTF/glTFExporter.h"
#include "AssetLib/glTF/glTFAssetWriter.h"
#include "PostProcessing/SplitLargeMeshes.h"

#include <assimp/commonMetaData.h>
#include <assimp/Exceptional.h>
#include <assimp/StringComparison.h>
#include <assimp/ByteSwapper.h>
#include <assimp/SceneCombiner.h>
#include <assimp/version.h>
#include <assimp/IOSystem.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/material.h>
#include <assimp/scene.h>

// Header files, standard library.
#include <memory>
#include <limits>
#include <inttypes.h>

#ifdef ASSIMP_IMPORTER_GLTF_USE_OPEN3DGC
	// Header files, Open3DGC.
#	include <Open3DGC/o3dgcSC3DMCEncoder.h>
#endif

using namespace rapidjson;

using namespace Assimp;
using namespace glTF;

namespace Assimp {

    // ------------------------------------------------------------------------------------------------
    // Worker function for exporting a scene to GLTF. Prototyped and registered in Exporter.cpp
    void ExportSceneGLTF(const char* pFile, IOSystem* pIOSystem, const aiScene* pScene, const ExportProperties* pProperties)
    {
        // invoke the exporter
        glTFExporter exporter(pFile, pIOSystem, pScene, pProperties, false);
    }

    // ------------------------------------------------------------------------------------------------
    // Worker function for exporting a scene to GLB. Prototyped and registered in Exporter.cpp
    void ExportSceneGLB(const char* pFile, IOSystem* pIOSystem, const aiScene* pScene, const ExportProperties* pProperties)
    {
        // invoke the exporter
        glTFExporter exporter(pFile, pIOSystem, pScene, pProperties, true);
    }

} // end of namespace Assimp

glTFExporter::glTFExporter(const char* filename, IOSystem* pIOSystem, const aiScene* pScene,
                           const ExportProperties* pProperties, bool isBinary)
    : mFilename(filename)
    , mIOSystem(pIOSystem)
    , mProperties(pProperties)
{
    aiScene* sceneCopy_tmp;
    SceneCombiner::CopyScene(&sceneCopy_tmp, pScene);

    SplitLargeMeshesProcess_Triangle tri_splitter;
    tri_splitter.SetLimit(0xffff);
    tri_splitter.Execute(sceneCopy_tmp);

    SplitLargeMeshesProcess_Vertex vert_splitter;
    vert_splitter.SetLimit(0xffff);
    vert_splitter.Execute(sceneCopy_tmp);

    mScene.reset(sceneCopy_tmp);

    mAsset = std::make_shared<glTF::Asset>(pIOSystem);

    if (isBinary) {
        mAsset->SetAsBinary();
    }

    ExportMetadata();

    //for (unsigned int i = 0; i < pScene->mNumCameras; ++i) {}

    //for (unsigned int i = 0; i < pScene->mNumLights; ++i) {}

    ExportMaterials();

    if (mScene->mRootNode) {
        ExportNodeHierarchy(mScene->mRootNode);
    }

    ExportMeshes();

    //for (unsigned int i = 0; i < pScene->mNumTextures; ++i) {}

    ExportScene();

    ExportAnimations();

    glTF::AssetWriter writer(*mAsset);

    if (isBinary) {
        writer.WriteGLBFile(filename);
    } else {
        writer.WriteFile(filename);
    }
}

/*
 * Copy a 4x4 matrix from struct aiMatrix to typedef mat4.
 * Also converts from row-major to column-major storage.
 */
static void CopyValue(const aiMatrix4x4& v, glTF::mat4& o)
{
    o[ 0] = v.a1; o[ 1] = v.b1; o[ 2] = v.c1; o[ 3] = v.d1;
    o[ 4] = v.a2; o[ 5] = v.b2; o[ 6] = v.c2; o[ 7] = v.d2;
    o[ 8] = v.a3; o[ 9] = v.b3; o[10] = v.c3; o[11] = v.d3;
    o[12] = v.a4; o[13] = v.b4; o[14] = v.c4; o[15] = v.d4;
}

static void CopyValue(const aiMatrix4x4& v, aiMatrix4x4& o)
{
    memcpy(&o, &v, sizeof(aiMatrix4x4));
}

static void IdentityMatrix4(glTF::mat4& o)
{
    o[ 0] = 1; o[ 1] = 0; o[ 2] = 0; o[ 3] = 0;
    o[ 4] = 0; o[ 5] = 1; o[ 6] = 0; o[ 7] = 0;
    o[ 8] = 0; o[ 9] = 0; o[10] = 1; o[11] = 0;
    o[12] = 0; o[13] = 0; o[14] = 0; o[15] = 1;
}

template<typename T>
void SetAccessorRange(Ref<Accessor> acc, void* data, unsigned int count,
	unsigned int numCompsIn, unsigned int numCompsOut)
{
	ai_assert(numCompsOut <= numCompsIn);

	// Allocate and initialize with large values.
	for (unsigned int i = 0 ; i < numCompsOut ; i++) {
		acc->min.push_back( std::numeric_limits<double>::max());
		acc->max.push_back(-std::numeric_limits<double>::max());
	}

	size_t totalComps = count * numCompsIn;
	T* buffer_ptr = static_cast<T*>(data);
	T* buffer_end = buffer_ptr + totalComps;

	// Search and set extreme values.
	for (; buffer_ptr < buffer_end ; buffer_ptr += numCompsIn) {
		for (unsigned int j = 0 ; j < numCompsOut ; j++) {
			double valueTmp = buffer_ptr[j];

			if (valueTmp < acc->min[j]) {
				acc->min[j] = valueTmp;
			}
			if (valueTmp > acc->max[j]) {
				acc->max[j] = valueTmp;
			}
		}
	}
}

inline void SetAccessorRange(ComponentType compType, Ref<Accessor> acc, void* data,
		unsigned int count, unsigned int numCompsIn, unsigned int numCompsOut)
{
	switch (compType) {
		case ComponentType_SHORT:
			SetAccessorRange<short>(acc, data, count, numCompsIn, numCompsOut);
			return;
		case ComponentType_UNSIGNED_SHORT:
			SetAccessorRange<unsigned short>(acc, data, count, numCompsIn, numCompsOut);
			return;
		case ComponentType_UNSIGNED_INT:
			SetAccessorRange<unsigned int>(acc, data, count, numCompsIn, numCompsOut);
			return;
		case ComponentType_FLOAT:
			SetAccessorRange<float>(acc, data, count, numCompsIn, numCompsOut);
			return;
		case ComponentType_BYTE:
			SetAccessorRange<int8_t>(acc, data, count, numCompsIn, numCompsOut);
			return;
		case ComponentType_UNSIGNED_BYTE:
			SetAccessorRange<uint8_t>(acc, data, count, numCompsIn, numCompsOut);
			return;
	}
}

inline Ref<Accessor> ExportData(Asset &a, std::string &meshName, Ref<Buffer> &buffer,
        unsigned int count, void *data, AttribType::Value typeIn, AttribType::Value typeOut, ComponentType compType, BufferViewTarget target = BufferViewTarget_NONE) {
    if (!count || !data) return Ref<Accessor>();

    unsigned int numCompsIn = AttribType::GetNumComponents(typeIn);
    unsigned int numCompsOut = AttribType::GetNumComponents(typeOut);
    unsigned int bytesPerComp = ComponentTypeSize(compType);

    size_t offset = buffer->byteLength;
    // make sure offset is correctly byte-aligned, as required by spec
    size_t padding = offset % bytesPerComp;
    offset += padding;
    size_t length = count * numCompsOut * bytesPerComp;
    buffer->Grow(length + padding);

    // bufferView
    Ref<BufferView> bv = a.bufferViews.Create(a.FindUniqueID(meshName, "view"));
    bv->buffer = buffer;
    bv->byteOffset = unsigned(offset);
    bv->byteLength = length; //! The target that the WebGL buffer should be bound to.
    bv->target = target;

    // accessor
    Ref<Accessor> acc = a.accessors.Create(a.FindUniqueID(meshName, "accessor"));
    acc->bufferView = bv;
    acc->byteOffset = 0;
    acc->byteStride = 0;
    acc->componentType = compType;
    acc->count = count;
    acc->type = typeOut;

    // calculate min and max values
	SetAccessorRange(compType, acc, data, count, numCompsIn, numCompsOut);

    // copy the data
    acc->WriteData(count, data, numCompsIn*bytesPerComp);

    return acc;
}

namespace {
    void GetMatScalar(const aiMaterial* mat, float& val, const char* propName, int type, int idx) {
        ai_assert( nullptr != mat );
        if ( nullptr != mat ) {
            mat->Get(propName, type, idx, val);
        }
    }
}

void glTFExporter::GetTexSampler(const aiMaterial* mat, glTF::TexProperty& prop)
{
    std::string samplerId = mAsset->FindUniqueID("", "sampler");
    prop.texture->sampler = mAsset->samplers.Create(samplerId);

    aiTextureMapMode mapU, mapV;
    aiGetMaterialInteger(mat,AI_MATKEY_MAPPINGMODE_U_DIFFUSE(0),(int*)&mapU);
    aiGetMaterialInteger(mat,AI_MATKEY_MAPPINGMODE_V_DIFFUSE(0),(int*)&mapV);

    switch (mapU) {
        case aiTextureMapMode_Wrap:
            prop.texture->sampler->wrapS = SamplerWrap_Repeat;
            break;
        case aiTextureMapMode_Clamp:
            prop.texture->sampler->wrapS = SamplerWrap_Clamp_To_Edge;
            break;
        case aiTextureMapMode_Mirror:
            prop.texture->sampler->wrapS = SamplerWrap_Mirrored_Repeat;
            break;
        case aiTextureMapMode_Decal:
        default:
            prop.texture->sampler->wrapS = SamplerWrap_Repeat;
            break;
    };

    switch (mapV) {
        case aiTextureMapMode_Wrap:
            prop.texture->sampler->wrapT = SamplerWrap_Repeat;
            break;
        case aiTextureMapMode_Clamp:
            prop.texture->sampler->wrapT = SamplerWrap_Clamp_To_Edge;
            break;
        case aiTextureMapMode_Mirror:
            prop.texture->sampler->wrapT = SamplerWrap_Mirrored_Repeat;
            break;
        case aiTextureMapMode_Decal:
        default:
            prop.texture->sampler->wrapT = SamplerWrap_Repeat;
            break;
    };

    // Hard coded Texture filtering options because I do not know where to find them in the aiMaterial.
    prop.texture->sampler->magFilter = SamplerMagFilter_Linear;
    prop.texture->sampler->minFilter = SamplerMinFilter_Linear;
}

void glTFExporter::GetMatColorOrTex(const aiMaterial* mat, glTF::TexProperty& prop,
        const char* propName, int type, int idx, aiTextureType tt) {
    aiString tex;
    aiColor4D col;
    if (mat->GetTextureCount(tt) > 0) {
        if (mat->Get(AI_MATKEY_TEXTURE(tt, 0), tex) == AI_SUCCESS) {
            std::string path = tex.C_Str();

            if (path.size() > 0) {
                if (path[0] != '*') {
                    std::map<std::string, unsigned int>::iterator it = mTexturesByPath.find(path);
                    if (it != mTexturesByPath.end()) {
                        prop.texture = mAsset->textures.Get(it->second);
                    }
                }

                if (!prop.texture) {
                    std::string texId = mAsset->FindUniqueID("", "texture");
                    prop.texture = mAsset->textures.Create(texId);
                    mTexturesByPath[path] = prop.texture.GetIndex();

                    std::string imgId = mAsset->FindUniqueID("", "image");
                    prop.texture->source = mAsset->images.Create(imgId);

                    if (path[0] == '*') { // embedded
                        aiTexture* curTex = mScene->mTextures[atoi(&path[1])];

                        prop.texture->source->name = curTex->mFilename.C_Str();

                        uint8_t *data = reinterpret_cast<uint8_t *>(curTex->pcData);
                        prop.texture->source->SetData(data, curTex->mWidth, *mAsset);

                        if (curTex->achFormatHint[0]) {
                            std::string mimeType = "image/";
                            mimeType += (memcmp(curTex->achFormatHint, "jpg", 3) == 0) ? "jpeg" : curTex->achFormatHint;
                            prop.texture->source->mimeType = mimeType;
                        }
                    } else {
                        prop.texture->source->uri = path;
                    }

                    GetTexSampler(mat, prop);
                }
            }
        }
    }

    if (mat->Get(propName, type, idx, col) == AI_SUCCESS) {
        prop.color[0] = col.r;
        prop.color[1] = col.g;
        prop.color[2] = col.b;
        prop.color[3] = col.a;
    }
}


void glTFExporter::ExportMaterials()
{
    aiString aiName;
    for (unsigned int i = 0; i < mScene->mNumMaterials; ++i) {
        const aiMaterial* mat = mScene->mMaterials[i];


        std::string name;
        if (mat->Get(AI_MATKEY_NAME, aiName) == AI_SUCCESS) {
            name = aiName.C_Str();
        }
        name = mAsset->FindUniqueID(name, "material");

        Ref<Material> m = mAsset->materials.Create(name);

        GetMatColorOrTex(mat, m->ambient, AI_MATKEY_COLOR_AMBIENT, aiTextureType_AMBIENT);
        GetMatColorOrTex(mat, m->diffuse, AI_MATKEY_COLOR_DIFFUSE, aiTextureType_DIFFUSE);
        GetMatColorOrTex(mat, m->specular, AI_MATKEY_COLOR_SPECULAR, aiTextureType_SPECULAR);
        GetMatColorOrTex(mat, m->emission, AI_MATKEY_COLOR_EMISSIVE, aiTextureType_EMISSIVE);

        m->transparent = mat->Get(AI_MATKEY_OPACITY, m->transparency) == aiReturn_SUCCESS && m->transparency != 1.0;

        GetMatScalar(mat, m->shininess, AI_MATKEY_SHININESS);
    }
}

/*
 * Search through node hierarchy and find the node containing the given meshID.
 * Returns true on success, and false otherwise.
 */
bool FindMeshNode(Ref<Node> &nodeIn, Ref<Node> &meshNode, const std::string &meshID) {
