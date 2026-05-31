#include "kskelanimation.h"
#include "kmesh.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>

#ifndef KEMENA_NO_ASSIMP
#include "kassimp_internal.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#endif

namespace kemena
{
#ifndef KEMENA_NO_ASSIMP
    // -----------------------------------------------------------------------
    // File-static helpers (private — keep all Assimp interaction inside the
    // import path so the public header stays Assimp-free).
    // -----------------------------------------------------------------------
    namespace
    {
        void readHierarchyData(kNodeData &dest, const aiNode *src)
        {
            if (!src) throw std::runtime_error("kSkeletalAnimation: null aiNode");

            dest.name           = src->mName.data;
            dest.transformation = kAssimpInternal::toMat4(src->mTransformation);
            dest.childrenCount  = (int)src->mNumChildren;

            for (unsigned int i = 0; i < src->mNumChildren; ++i)
            {
                kNodeData child;
                readHierarchyData(child, src->mChildren[i]);
                dest.children.push_back(std::move(child));
            }
        }

        // Walks `setMesh` recursively, creating a kBone per channel and
        // updating the mesh's bone-info map with any joints not yet known.
        void bindAnimationToMesh(const aiAnimation *animation,
                                 kMesh *setMesh,
                                 std::vector<kBone> &bones,
                                 std::vector<kMesh *> &meshes)
        {
            if (!setMesh) return;
            meshes.push_back(setMesh);

            if (!setMesh->getVertices().empty())
            {
                std::map<kString, kBoneInfo> &meshBoneInfoMap = setMesh->getBoneInfoMap();
                int boneCount = setMesh->getBoneCount();

                for (unsigned int i = 0; i < animation->mNumChannels; ++i)
                {
                    auto *channel = animation->mChannels[i];
                    if (!channel ||
                        (channel->mNumPositionKeys == 0 &&
                         channel->mNumRotationKeys == 0 &&
                         channel->mNumScalingKeys  == 0))
                    {
                        std::cerr << "kSkeletalAnimation: invalid channel " << i << "\n";
                        continue;
                    }

                    kString boneName = channel->mNodeName.data;
                    if (meshBoneInfoMap.find(boneName) == meshBoneInfoMap.end())
                    {
                        meshBoneInfoMap[boneName].id = boneCount;
                        ++boneCount;
                    }
                    bones.emplace_back(boneName,
                                       meshBoneInfoMap[boneName].id,
                                       channel);
                }
            }

            for (kObject *child : setMesh->getChildren())
                if (child && child->getType() == NODE_TYPE_MESH)
                    bindAnimationToMesh(animation, (kMesh *)child, bones, meshes);
        }
    }

    // -----------------------------------------------------------------------
    // kSkeletalAnimation
    // -----------------------------------------------------------------------

    kSkeletalAnimation::kSkeletalAnimation(const kString &animationPath, kMesh *setMesh)
    {
        Assimp::Importer importer;
        const aiScene *scene = importer.ReadFile(
            animationPath, aiProcess_Triangulate | aiProcess_LimitBoneWeights);
        assert(scene && scene->mRootNode);

        if (scene->mNumAnimations == 0)
            throw std::runtime_error("kSkeletalAnimation: no animations in " + animationPath);

        const aiAnimation *animation = scene->mAnimations[0];
        duration       = (float)animation->mDuration;
        ticksPerSecond = (int)animation->mTicksPerSecond;

        readHierarchyData(rootNode, scene->mRootNode);
        bindAnimationToMesh(animation, setMesh, bones, meshes);
    }
#endif // KEMENA_NO_ASSIMP

    kSkeletalAnimation::kSkeletalAnimation() = default;

    kBone *kSkeletalAnimation::findBone(const kString &name)
    {
        auto it = std::find_if(bones.begin(), bones.end(),
                               [&](const kBone &b){ return b.getName() == name; });
        return it == bones.end() ? nullptr : &(*it);
    }

    float            kSkeletalAnimation::getTicksPerSecond() const { return (float)ticksPerSecond; }
    float            kSkeletalAnimation::getDuration()       const { return duration; }
    const kNodeData &kSkeletalAnimation::getRootNode()       const { return rootNode; }
    std::vector<kMesh *> kSkeletalAnimation::getMeshes()           { return meshes; }
    void             kSkeletalAnimation::setSpeed(float s)         { speed = s; }
    float            kSkeletalAnimation::getSpeed()         const  { return speed; }
}
