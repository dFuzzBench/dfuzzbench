/*
---------------------------------------------------------------------------
Open Asset Import Library (assimp)
---------------------------------------------------------------------------

Copyright (c) 2006-2020, assimp team



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

/** @file  ValidateDataStructure.cpp
 *  @brief Implementation of the post processing step to validate
 *    the data structure returned by Assimp.
 */

// internal headers
#include "ValidateDataStructure.h"
#include "ProcessHelper.h"
#include <assimp/BaseImporter.h>
#include <assimp/fast_atof.h>
#include <memory>

// CRT headers
#include <stdarg.h>

using namespace Assimp;

// ------------------------------------------------------------------------------------------------
// Constructor to be privately used by Importer
ValidateDSProcess::ValidateDSProcess() :
        mScene() {}

// ------------------------------------------------------------------------------------------------
// Destructor, private as well
ValidateDSProcess::~ValidateDSProcess() {}

// ------------------------------------------------------------------------------------------------
// Returns whether the processing step is present in the given flag field.
bool ValidateDSProcess::IsActive(unsigned int pFlags) const {
    return (pFlags & aiProcess_ValidateDataStructure) != 0;
}
// ------------------------------------------------------------------------------------------------
AI_WONT_RETURN void ValidateDSProcess::ReportError(const char *msg, ...) {
    ai_assert(nullptr != msg);

    va_list args;
    va_start(args, msg);

    char szBuffer[3000];
    const int iLen = vsprintf(szBuffer, msg, args);
    ai_assert(iLen > 0);

    va_end(args);

    throw DeadlyImportError("Validation failed: " + std::string(szBuffer, iLen));
}
// ------------------------------------------------------------------------------------------------
void ValidateDSProcess::ReportWarning(const char *msg, ...) {
    ai_assert(nullptr != msg);

    va_list args;
    va_start(args, msg);

    char szBuffer[3000];
    const int iLen = vsprintf(szBuffer, msg, args);
    ai_assert(iLen > 0);

    va_end(args);
    ASSIMP_LOG_WARN("Validation warning: " + std::string(szBuffer, iLen));
}

// ------------------------------------------------------------------------------------------------
inline int HasNameMatch(const aiString &in, aiNode *node) {
    int result = (node->mName == in ? 1 : 0);
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        result += HasNameMatch(in, node->mChildren[i]);
    }
    return result;
}

// ------------------------------------------------------------------------------------------------
template <typename T>
inline void ValidateDSProcess::DoValidation(T **parray, unsigned int size, const char *firstName, const char *secondName) {
    // validate all entries
    if (size) {
        if (!parray) {
            ReportError("aiScene::%s is nullptr (aiScene::%s is %i)",
                    firstName, secondName, size);
        }
        for (unsigned int i = 0; i < size; ++i) {
            if (!parray[i]) {
                ReportError("aiScene::%s[%i] is nullptr (aiScene::%s is %i)",
                        firstName, i, secondName, size);
            }
            Validate(parray[i]);
        }
    }
}

// ------------------------------------------------------------------------------------------------
template <typename T>
inline void ValidateDSProcess::DoValidationEx(T **parray, unsigned int size,
        const char *firstName, const char *secondName) {
    // validate all entries
    if (size) {
        if (!parray) {
            ReportError("aiScene::%s is nullptr (aiScene::%s is %i)",
                    firstName, secondName, size);
        }
        for (unsigned int i = 0; i < size; ++i) {
            if (!parray[i]) {
                ReportError("aiScene::%s[%u] is nullptr (aiScene::%s is %u)",
                        firstName, i, secondName, size);
            }
            Validate(parray[i]);

            // check whether there are duplicate names
            for (unsigned int a = i + 1; a < size; ++a) {
                if (parray[i]->mName == parray[a]->mName) {
                    ReportError("aiScene::%s[%u] has the same name as "
                                "aiScene::%s[%u]",
                            firstName, i, secondName, a);
                }
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------
template <typename T>
inline void ValidateDSProcess::DoValidationWithNameCheck(T **array, unsigned int size, const char *firstName,
        const char *secondName) {
    // validate all entries
    DoValidationEx(array, size, firstName, secondName);

    for (unsigned int i = 0; i < size; ++i) {
        int res = HasNameMatch(array[i]->mName, mScene->mRootNode);
        if (0 == res) {
            const std::string name = static_cast<char *>(array[i]->mName.data);
            ReportError("aiScene::%s[%i] has no corresponding node in the scene graph (%s)",
                    firstName, i, name.c_str());
        } else if (1 != res) {
            const std::string name = static_cast<char *>(array[i]->mName.data);
            ReportError("aiScene::%s[%i]: there are more than one nodes with %s as name",
                    firstName, i, name.c_str());
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Executes the post processing step on the given imported data.
void ValidateDSProcess::Execute(aiScene *pScene) {
    mScene = pScene;
    ASSIMP_LOG_DEBUG("ValidateDataStructureProcess begin");

    // validate the node graph of the scene
    Validate(pScene->mRootNode);

    // validate all meshes
    if (pScene->mNumMeshes) {
        DoValidation(pScene->mMeshes, pScene->mNumMeshes, "mMeshes", "mNumMeshes");
    } else if (!(mScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        ReportError("aiScene::mNumMeshes is 0. At least one mesh must be there");
    } else if (pScene->mMeshes) {
        ReportError("aiScene::mMeshes is non-null although there are no meshes");
    }

    // validate all animations
    if (pScene->mNumAnimations) {
        DoValidation(pScene->mAnimations, pScene->mNumAnimations,
                "mAnimations", "mNumAnimations");
    } else if (pScene->mAnimations) {
        ReportError("aiScene::mAnimations is non-null although there are no animations");
    }

    // validate all cameras
    if (pScene->mNumCameras) {
        DoValidationWithNameCheck(pScene->mCameras, pScene->mNumCameras,
                "mCameras", "mNumCameras");
    } else if (pScene->mCameras) {
        ReportError("aiScene::mCameras is non-null although there are no cameras");
    }

    // validate all lights
    if (pScene->mNumLights) {
        DoValidationWithNameCheck(pScene->mLights, pScene->mNumLights,
                "mLights", "mNumLights");
    } else if (pScene->mLights) {
        ReportError("aiScene::mLights is non-null although there are no lights");
    }

    // validate all textures
    if (pScene->mNumTextures) {
        DoValidation(pScene->mTextures, pScene->mNumTextures,
                "mTextures", "mNumTextures");
    } else if (pScene->mTextures) {
        ReportError("aiScene::mTextures is non-null although there are no textures");
    }

    // validate all materials
    if (pScene->mNumMaterials) {
        DoValidation(pScene->mMaterials, pScene->mNumMaterials, "mMaterials", "mNumMaterials");
    }
#if 0
    // NOTE: ScenePreprocessor generates a default material if none is there
    else if (!(mScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        ReportError("aiScene::mNumMaterials is 0. At least one material must be there");
    }
#endif
    else if (pScene->mMaterials) {
        ReportError("aiScene::mMaterials is non-null although there are no materials");
    }

    //  if (!has)ReportError("The aiScene data structure is empty");
    ASSIMP_LOG_DEBUG("ValidateDataStructureProcess end");
}

// ------------------------------------------------------------------------------------------------
void ValidateDSProcess::Validate(const aiLight *pLight) {
    if (pLight->mType == aiLightSource_UNDEFINED)
        ReportWarning("aiLight::mType is aiLightSource_UNDEFINED");

    if (!pLight->mAttenuationConstant &&
            !pLight->mAttenuationLinear &&
            !pLight->mAttenuationQuadratic) {
        ReportWarning("aiLight::mAttenuationXXX - all are zero");
    }

    if (pLight->mAngleInnerCone > pLight->mAngleOuterCone)
        ReportError("aiLight::mAngleInnerCone is larger than aiLight::mAngleOuterCone");

    if (pLight->mColorDiffuse.IsBlack() && pLight->mColorAmbient.IsBlack() && pLight->mColorSpecular.IsBlack()) {
        ReportWarning("aiLight::mColorXXX - all are black and won't have any influence");
    }
}

// ------------------------------------------------------------------------------------------------
void ValidateDSProcess::Validate(const aiCamera *pCamera) {
    if (pCamera->mClipPlaneFar <= pCamera->mClipPlaneNear)
        ReportError("aiCamera::mClipPlaneFar must be >= aiCamera::mClipPlaneNear");

    // FIX: there are many 3ds files with invalid FOVs. No reason to
    // reject them at all ... a warning is appropriate.
    if (!pCamera->mHorizontalFOV || pCamera->mHorizontalFOV >= (float)AI_MATH_PI)
        ReportWarning("%f is not a valid value for aiCamera::mHorizontalFOV", pCamera->mHorizontalFOV);
}

// ------------------------------------------------------------------------------------------------
void ValidateDSProcess::Validate(const aiMesh *pMesh) {
    // validate the material index of the mesh
    if (mScene->mNumMaterials && pMesh->mMaterialIndex >= mScene->mNumMaterials) {
        ReportError("aiMesh::mMaterialIndex is invalid (value: %i maximum: %i)",
                pMesh->mMaterialIndex, mScene->mNumMaterials - 1);
    }

    Validate(&pMesh->mName);

    for (unsigned int i = 0; i < pMesh->mNumFaces; ++i) {
        aiFace &face = pMesh->mFaces[i];

        if (pMesh->mPrimitiveTypes) {
            switch (face.mNumIndices) {
            case 0:
                ReportError("aiMesh::mFaces[%i].mNumIndices is 0", i);
                break;
            case 1:
                if (0 == (pMesh->mPrimitiveTypes & aiPrimitiveType_POINT)) {
                    ReportError("aiMesh::mFaces[%i] is a POINT but aiMesh::mPrimitiveTypes "
                                "does not report the POINT flag",
                            i);
                }
                break;
            case 2:
                if (0 == (pMesh->mPrimitiveTypes & aiPrimitiveType_LINE)) {
                    ReportError("aiMesh::mFaces[%i] is a LINE but aiMesh::mPrimitiveTypes "
                                "does not report the LINE flag",
                            i);
                }
                break;
            case 3:
                if (0 == (pMesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)) {
                    ReportError("aiMesh::mFaces[%i] is a TRIANGLE but aiMesh::mPrimitiveTypes "
                                "does not report the TRIANGLE flag",
                            i);
                }
                break;
            default:
                if (0 == (pMesh->mPrimitiveTypes & aiPrimitiveType_POLYGON)) {
                    this->ReportError("aiMesh::mFaces[%i] is a POLYGON but aiMesh::mPrimitiveTypes "
                                      "does not report the POLYGON flag",
                            i);
                }
                break;
            };
        }

        if (!face.mIndices)
            ReportError("aiMesh::mFaces[%i].mIndices is nullptr", i);
    }

    // positions must always be there ...
    if (!pMesh->mNumVertices || (!pMesh->mVertices && !mScene->mFlags)) {
        ReportError("The mesh %s contains no vertices", pMesh->mName.C_Str());
    }

    if (pMesh->mNumVertices > AI_MAX_VERTICES) {
        ReportError("Mesh has too many vertices: %u, but the limit is %u", pMesh->mNumVertices, AI_MAX_VERTICES);
    }
    if (pMesh->mNumFaces > AI_MAX_FACES) {
        ReportError("Mesh has too many faces: %u, but the limit is %u", pMesh->mNumFaces, AI_MAX_FACES);
    }

    // if tangents are there there must also be bitangent vectors ...
    if ((pMesh->mTangents != nullptr) != (pMesh->mBitangents != nullptr)) {
        ReportError("If there are tangents, bitangent vectors must be present as well");
    }

    // faces, too
    if (!pMesh->mNumFaces || (!pMesh->mFaces && !mScene->mFlags)) {
        ReportError("Mesh %s contains no faces", pMesh->mName.C_Str());
    }

    // now check whether the face indexing layout is correct:
    // unique vertices, pseudo-indexed.
    std::vector<bool> abRefList;
    abRefList.resize(pMesh->mNumVertices, false);
    for (unsigned int i = 0; i < pMesh->mNumFaces; ++i) {
        aiFace &face = pMesh->mFaces[i];
        if (face.mNumIndices > AI_MAX_FACE_INDICES) {
            ReportError("Face %u has too many faces: %u, but the limit is %u", i, face.mNumIndices, AI_MAX_FACE_INDICES);
        }

        for (unsigned int a = 0; a < face.mNumIndices; ++a) {
            if (face.mIndices[a] >= pMesh->mNumVertices) {
                ReportError("aiMesh::mFaces[%i]::mIndices[%i] is out of range", i, a);
            }
            // the MSB flag is temporarily used by the extra verbose
            // mode to tell us that the JoinVerticesProcess might have
            // been executed already.
            /*if ( !(this->mScene->mFlags & AI_SCENE_FLAGS_NON_VERBOSE_FORMAT ) && !(this->mScene->mFlags & AI_SCENE_FLAGS_ALLOW_SHARED) &&
				abRefList[face.mIndices[a]])
            {
                ReportError("aiMesh::mVertices[%i] is referenced twice - second "
                    "time by aiMesh::mFaces[%i]::mIndices[%i]",face.mIndices[a],i,a);
            }*/
            abRefList[face.mIndices[a]] = true;
        }
    }

    // check whether there are vertices that aren't referenced by a face
    bool b = false;
    for (unsigned int i = 0; i < pMesh->mNumVertices; ++i) {
        if (!abRefList[i]) b = true;
    }
    abRefList.clear();
    if (b) {
        ReportWarning("There are unreferenced vertices");
    }

    // texture channel 2 may not be set if channel 1 is zero ...
    {
        unsigned int i = 0;
        for (; i < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++i) {
            if (!pMesh->HasTextureCoords(i)) break;
        }
        for (; i < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++i)
            if (pMesh->HasTextureCoords(i)) {
                ReportError("Texture coordinate channel %i exists "
                            "although the previous channel was nullptr.",
                        i);
            }
    }
    // the same for the vertex colors
    {
        unsigned int i = 0;
        for (; i < AI_MAX_NUMBER_OF_COLOR_SETS; ++i) {
            if (!pMesh->HasVertexColors(i)) break;
        }
        for (; i < AI_MAX_NUMBER_OF_COLOR_SETS; ++i)
            if (pMesh->HasVertexColors(i)) {
                ReportError("Vertex color channel %i is exists "
                            "although the previous channel was nullptr.",
                        i);
            }
    }

    // now validate all bones
    if (pMesh->mNumBones) {
        if (!pMesh->mBones) {
            ReportError("aiMesh::mBones is nullptr (aiMesh::mNumBones is %i)",
                    pMesh->mNumBones);
        }
        std::unique_ptr<float[]> afSum(nullptr);
        if (pMesh->mNumVertices) {
            afSum.reset(new float[pMesh->mNumVertices]);
            for (unsigned int i = 0; i < pMesh->mNumVertices; ++i)
                afSum[i] = 0.0f;
        }

        // check whether there are duplicate bone names
        for (unsigned int i = 0; i < pMesh->mNumBones; ++i) {
            const aiBone *bone = pMesh->mBones[i];
            if (bone->mNumWeights > AI_MAX_BONE_WEIGHTS) {
                ReportError("Bone %u has too many weights: %u, but the limit is %u", i, bone->mNumWeights, AI_MAX_BONE_WEIGHTS);
            }

            if (!pMesh->mBones[i]) {
                ReportError("aiMesh::mBones[%i] is nullptr (aiMesh::mNumBones is %i)",
                        i, pMesh->mNumBones);
            }
            Validate(pMesh, pMesh->mBones[i], afSum.get());

            for (unsigned int a = i + 1; a < pMesh->mNumBones; ++a) {
                if (pMesh->mBones[i]->mName == pMesh->mBones[a]->mName) {
                    const char *name = "unknown";
                    if (nullptr != pMesh->mBones[i]->mName.C_Str()) {
                        name = pMesh->mBones[i]->mName.C_Str();
                    }
                    ReportError("aiMesh::mBones[%i], name = \"%s\" has the same name as "
                                "aiMesh::mBones[%i]",
                            i, name, a);
                }
            }
        }
        // check whether all bone weights for a vertex sum to 1.0 ...
        for (unsigned int i = 0; i < pMesh->mNumVertices; ++i) {
            if (afSum[i] && (afSum[i] <= 0.94 || afSum[i] >= 1.05)) {
                ReportWarning("aiMesh::mVertices[%i]: bone weight sum != 1.0 (sum is %f)", i, afSum[i]);
            }
        }
    } else if (pMesh->mBones) {
        ReportError("aiMesh::mBones is non-null although there are no bones");
    }
}

// ------------------------------------------------------------------------------------------------
void ValidateDSProcess::Validate(const aiMesh *pMesh, const aiBone *pBone, float *afSum) {
    this->Validate(&pBone->mName);

    if (!pBone->mNumWeights) {
        //ReportError("aiBone::mNumWeights is zero");
    }

    // check whether all vertices affected by this bone are valid
    for (unsigned int i = 0; i < pBone->mNumWeights; ++i) {
        if (pBone->mWeights[i].mVertexId >= pMesh->mNumVertices) {
            ReportError("aiBone::mWeights[%i].mVertexId is out of range", i);
        } else if (!pBone->mWeights[i].mWeight || pBone->mWeights[i].mWeight > 1.0f) {
            ReportWarning("aiBone::mWeights[%i].mWeight has an invalid value", i);
        }
        afSum[pBone->mWeights[i].mVertexId] += pBone->mWeights[i].mWeight;
    }
}

// ------------------------------------------------------------------------------------------------
void ValidateDSProcess::Validate(const aiAnimation *pAnimation) {
    Validate(&pAnimation->mName);

    // validate all animations
    if (pAnimation->mNumChannels || pAnimation->mNumMorphMeshChannels) {
        if (!pAnimation->mChannels && pAnimation->mNumChannels) {
            ReportError("aiAnimation::mChannels is nullptr (aiAnimation::mNumChannels is %i)",
                    pAnimation->mNumChannels);
        }
        if (!pAnimation->mMorphMeshChannels && pAnimation->mNumMorphMeshChannels) {
