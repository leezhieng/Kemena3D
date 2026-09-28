/**
 * @file kanimator_blend.cpp
 * @brief Out-of-line implementation of kAnimator::calculateBlendedBoneTransform().
 *
 * The animator-controller blend tree needs to interpolate an arbitrary number
 * of skeletal clips at once. kAnimator only supported a two-clip cross-fade
 * (beginBlend), so a weighted multi-clip pose pass was added to the engine
 * header. That declaration is implemented here — in the editor project rather
 * than in the prebuilt SDK static library — so this workspace does not require
 * rebuilding the SDK. Defining an existing class member out of line leaves
 * kAnimator's layout and ABI untouched, and the header is kept consistent for
 * future SDK rebuilds.
 */

#include "kemena/kemena.h"

#include <glm/gtc/quaternion.hpp>

#include <map>
#include <vector>

namespace kemena
{
    namespace
    {
        /** @brief Decomposes a 4x4 transform into translation / rotation / scale. */
        void blendDecomposeTRS(const kMat4 &m, kVec3 &translation, kQuat &rotation, kVec3 &scale)
        {
            translation = kVec3(m[3][0], m[3][1], m[3][2]);
            scale = kVec3(glm::length(kVec3(m[0])),
                          glm::length(kVec3(m[1])),
                          glm::length(kVec3(m[2])));

            // Rebuild the pure rotation columns (divide the scale out) so
            // non-unit scale does not leak into the quaternion extraction.
            kMat3 rot(1.0f);
            rot[0] = (scale.x > 1e-6f) ? kVec3(m[0]) / scale.x : kVec3(m[0]);
            rot[1] = (scale.y > 1e-6f) ? kVec3(m[1]) / scale.y : kVec3(m[1]);
            rot[2] = (scale.z > 1e-6f) ? kVec3(m[2]) / scale.z : kVec3(m[2]);
            rotation = kQuat(rot);
        }
    }

    void kAnimator::calculateBlendedBoneTransform(const std::vector<kPoseSample> &samples,
                                                  const kNodeData *node, kMat4 parentTransform)
    {
        if (node == nullptr || samples.empty())
            return;

        kMat4 nodeTransform = node->transformation;

        // Root motion is never averaged across the blended clips — mixing
        // opposing translations would cancel them out and lose the delta.
        // Instead the root-motion bone is taken verbatim from the animator's
        // active clip (the dominant sample, set by the caller) and run through
        // the same extract/bake path as single-clip playback, so the character
        // still moves and getRootMotionDeltaPosition() reports the motion.
        bool rootMotionApplied = false;
        if (rootMotionActive())
        {
            resolveRootBone();
            if (node->name == rootBoneName && currentAnimation != nullptr)
            {
                kBone *rootBone = currentAnimation->findBone(node->name);
                if (rootBone != nullptr)
                {
                    rootBone->update(currentTime);
                    nodeTransform = rootBone->getLocalTransform();
                    handleRootMotion(rootBone, nodeTransform);
                    rootMotionApplied = true;
                }
            }
        }

        kVec3 posAccum(0.0f);
        kVec3 scaleAccum(0.0f);
        kQuat rotAccum(0.0f, 0.0f, 0.0f, 0.0f);
        kQuat rotRef(1.0f, 0.0f, 0.0f, 0.0f);
        float totalWeight = 0.0f;
        bool  haveRef     = false;

        if (!rootMotionApplied)
        for (const kPoseSample &s : samples)
        {
            if (s.animation == nullptr || s.weight <= 0.0f)
                continue;

            kBone *bone = s.animation->findBone(node->name);
            if (bone == nullptr)
                continue;

            bone->update(s.time);

            kVec3 t, sc;
            kQuat r;
            blendDecomposeTRS(bone->getLocalTransform(), t, r, sc);

            // Keep every contribution on the same quaternion hemisphere as the
            // first one, otherwise opposite-sign quaternions cancel out.
            if (!haveRef) { rotRef = r; haveRef = true; }
            else if (glm::dot(r, rotRef) < 0.0f) r = -r;

            posAccum    += t * s.weight;
            scaleAccum  += sc * s.weight;
            rotAccum    += r * s.weight;
            totalWeight += s.weight;
        }

        if (totalWeight > 1e-6f)
        {
            const kVec3 pos    = posAccum / totalWeight;
            const kVec3 scale  = scaleAccum / totalWeight;
            const float rotLen = glm::length(rotAccum);
            const kQuat rot    = (rotLen > 1e-6f) ? (rotAccum / rotLen) : rotRef;

            // Compose T * R * S without glm::translate/scale so this file does
            // not need the matrix_transform extension included.
            kMat4 composed(1.0f);
            composed[0] = kVec4(rot * kVec3(scale.x, 0.0f, 0.0f), 0.0f);
            composed[1] = kVec4(rot * kVec3(0.0f, scale.y, 0.0f), 0.0f);
            composed[2] = kVec4(rot * kVec3(0.0f, 0.0f, scale.z), 0.0f);
            composed[3] = kVec4(pos, 1.0f);
            nodeTransform = composed;
        }

        kMat4 globalTransformation = parentTransform * nodeTransform;

        // All clips of a blend tree share the same skinned mesh / bone palette,
        // so any sample's mesh list resolves the bone indices.
        kSkeletalAnimation *meshClip = nullptr;
        for (const kPoseSample &s : samples)
            if (s.animation != nullptr) { meshClip = s.animation; break; }

        if (meshClip != nullptr)
        {
            const auto &meshes = meshClip->getMeshes();
            for (size_t i = 0; i < meshes.size(); ++i)
            {
                if (!meshes[i] || meshes[i]->getType() != kNodeType::NODE_TYPE_MESH)
                    continue;

                kMesh *childMesh = (kMesh *)meshes[i];
                std::map<kString, kBoneInfo> &boneInfoMap = childMesh->getBoneInfoMap();
                auto it = boneInfoMap.find(node->name);
                if (it != boneInfoMap.end())
                {
                    int   index  = it->second.id;
                    kMat4 offset = it->second.offset;
                    if (index >= 0 && index < (int)finalBoneMatrices.size())
                        finalBoneMatrices[index] = globalTransformation * offset;
                }
            }
        }

        for (int i = 0; i < node->childrenCount; ++i)
            calculateBlendedBoneTransform(samples, &node->children[i], globalTransformation);
    }
}
