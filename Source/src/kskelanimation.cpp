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

        // Collects every kMesh node in the hierarchy. The final bone matrices
        // are written later by matching animation node names against each
        // mesh's own (already-populated) bone-info map.
        void collectMeshes(kMesh *setMesh, std::vector<kMesh *> &meshes)
        {
            if (!setMesh) return;
            meshes.push_back(setMesh);
            for (kObject *child : setMesh->getChildren())
                if (child && child->getType() == NODE_TYPE_MESH)
                    collectMeshes(static_cast<kMesh *>(child), meshes);
        }

        // Creates one kBone per animation channel so the animator can look up
        // an animated node by name. The bone id stored on kBone is unused —
        // the animator resolves palette indices by matching node names against
        // each mesh's bone-info map, so we must NOT insert extra entries here.
        void bindAnimationToMesh(const aiAnimation *animation,
                                 std::vector<kBone> &bones)
        {
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
                bones.emplace_back(boneName, 0, channel);
            }
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

        if (!scene || !scene->mRootNode)
            throw std::runtime_error("kSkeletalAnimation: failed to load " + animationPath);

        if (scene->mNumAnimations == 0)
            throw std::runtime_error("kSkeletalAnimation: no animations in " + animationPath);

        const aiAnimation *animation = scene->mAnimations[0];
        duration       = (float)animation->mDuration;
        ticksPerSecond = (int)animation->mTicksPerSecond;

        readHierarchyData(rootNode, scene->mRootNode);
        collectMeshes(setMesh, meshes);
        bindAnimationToMesh(animation, bones);
    }
#endif // KEMENA_NO_ASSIMP

    kSkeletalAnimation::kSkeletalAnimation() = default;

    static void scaleNodeDataTranslations(kNodeData &node, float scale)
    {
        node.transformation[3][0] *= scale;
        node.transformation[3][1] *= scale;
        node.transformation[3][2] *= scale;
        for (auto &child : node.children)
            scaleNodeDataTranslations(child, scale);
    }

    void kSkeletalAnimation::applyTranslationScale(float scale)
    {
        if (scale == 1.0f)
            return;
        scaleNodeDataTranslations(rootNode, scale);
        for (auto &bone : bones)
            bone.scalePositions(scale);
    }

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

    void  kSkeletalAnimation::setRootMotionRotation(bool enabled)  { rootMotionRotation = enabled; }
    bool  kSkeletalAnimation::getRootMotionRotation() const        { return rootMotionRotation; }
    void  kSkeletalAnimation::setRootMotionPositionY(bool enabled) { rootMotionPositionY = enabled; }
    bool  kSkeletalAnimation::getRootMotionPositionY() const       { return rootMotionPositionY; }
    void  kSkeletalAnimation::setRootMotionPositionXZ(bool enabled){ rootMotionPositionXZ = enabled; }
    bool  kSkeletalAnimation::getRootMotionPositionXZ() const      { return rootMotionPositionXZ; }
}
