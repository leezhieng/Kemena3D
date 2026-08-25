#include "kanimator.h"

// Root-motion math: translate/scale matrix helpers and quaternion
// constructors (mat3→quat, mat4←quat) used by handleRootMotion().
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace kemena
{
    kAnimator::kAnimator(kSkeletalAnimation *newAnimation)
    {
        currentTime = 0.0f;

        if (newAnimation != nullptr)
        {
            currentAnimation = newAnimation;
            animations.push_back(newAnimation);
        }

        finalBoneMatrices.reserve(MAX_BONES);
        for (int i = 0; i < MAX_BONES; i++)
            finalBoneMatrices.push_back(kMat4(1.0f));
    }

    void kAnimator::addAnimation(kSkeletalAnimation *newAnimation)
    {
        animations.push_back(newAnimation);
    }

    void kAnimator::updateAnimation(float newDeltaTime, int frameId)
    {
        deltaTime = newDeltaTime;
        if (currentAnimation != nullptr && currentFrameId != frameId)
        {
            const float prevTime = currentTime;
            currentTime += currentAnimation->getTicksPerSecond() * newDeltaTime;
            currentTime = fmod(currentTime, currentAnimation->getDuration());

            // When the clip loops (time wrapped past the end), re-seed the
            // root-motion tracker. Otherwise the end-of-clip → start-of-clip
            // pose jump is accumulated as a huge delta that cancels out all
            // the motion gathered over the previous loop, snapping the object
            // back to its starting position.
            if (rootMotionActive() && currentTime < prevTime)
                resetRootMotion();

            const kNodeData &rootNode = currentAnimation->getRootNode();
            calculateBoneTransform(&rootNode, kMat4(1.0f));
        }
        // Only update once per frame.
        currentFrameId = frameId;

        // Future: sample objectAnimation tracks here and write per-target
        // transforms back to kObject — left as a hook for the cinematic
        // editor pass.
    }

    void kAnimator::playAnimation(kSkeletalAnimation *animation)
    {
        if (animation != nullptr)
        {
            currentAnimation = animation;
            currentTime = 0.0f;
            resetRootMotion();
        }
    }

    void kAnimator::playAnimation(int index)
    {
        kSkeletalAnimation *clip = getAnimation(index);
        if (clip != nullptr)
        {
            currentAnimation = clip;
            currentTime = 0.0f;
            resetRootMotion();
        }
    }

    kSkeletalAnimation *kAnimator::getCurrentAnimation()
    {
        return currentAnimation;
    }

    void kAnimator::calculateBoneTransform(const kNodeData *node, kMat4 parentTransform)
    {
        if (node == nullptr) return;
        if (currentAnimation == nullptr) return;

        kString nodeName      = node->name;
        kMat4   nodeTransform = node->transformation;

        kBone *bone = currentAnimation->findBone(nodeName);
        if (bone != nullptr)
        {
            bone->update(currentTime);
            nodeTransform = bone->getLocalTransform();

            // Root-motion extraction: the topmost animated bone is the root
            // bone. When any channel is enabled, resolve it once and then
            // accumulate the per-frame delta + bake the enabled channels out
            // of the pose so the character stays in place.
            if (rootMotionActive())
            {
                resolveRootBone();
                if (nodeName == rootBoneName)
                    handleRootMotion(bone, nodeTransform);
            }
        }

        kMat4 globalTransformation = parentTransform * nodeTransform;

        const auto &meshes = currentAnimation->getMeshes();
        for (size_t i = 0; i < meshes.size(); ++i)
        {
            if (!meshes[i] || meshes[i]->getType() != kNodeType::NODE_TYPE_MESH)
                continue;

            kMesh *childMesh = (kMesh *)meshes[i];
            std::map<kString, kBoneInfo> &boneInfoMap = childMesh->getBoneInfoMap();
            auto it = boneInfoMap.find(nodeName);
            if (it != boneInfoMap.end())
            {
                int   index   = it->second.id;
                kMat4 offset  = it->second.offset;
                // Guard against skeletons larger than MAX_BONES: the renderer
                // can only upload MAX_BONES matrices, so clamp the write to the
                // allocated palette instead of indexing out of bounds.
                if (index >= 0 && index < (int)finalBoneMatrices.size())
                    finalBoneMatrices[index] = globalTransformation * offset;
            }
        }

        for (int i = 0; i < node->childrenCount; ++i)
            calculateBoneTransform(&node->children[i], globalTransformation);
    }

    const std::vector<kMat4> kAnimator::getFinalBoneMatrices() const
    {
        return finalBoneMatrices;
    }

    void kAnimator::setCurrentTime(float newTime)
    {
        currentTime = newTime;
    }

    void kAnimator::setSpeed(float newSpeed)
    {
        speed = newSpeed;
    }

    float kAnimator::getSpeed()
    {
        return speed;
    }

    // -----------------------------------------------------------------------
    // Object-transform animation — placeholder for future cinematic editor.
    // The setters wire the clip in but updateAnimation() doesn't sample
    // tracks yet.
    // -----------------------------------------------------------------------

    void kAnimator::setObjectAnimation(kAnimation *clip)
    {
        objectAnimation = clip;
    }

    kAnimation *kAnimator::getObjectAnimation() const
    {
        return objectAnimation;
    }

    // -----------------------------------------------------------------------
    // Root-motion extraction.
    // -----------------------------------------------------------------------

    bool kAnimator::rootMotionActive() const
    {
        return currentAnimation != nullptr &&
               (currentAnimation->getRootMotionRotation() ||
                currentAnimation->getRootMotionPositionY() ||
                currentAnimation->getRootMotionPositionXZ());
    }

    bool kAnimator::isRootMotionActive() const
    {
        return rootMotionActive();
    }

    void kAnimator::resolveRootBone()
    {
        // Already resolved for this clip — nothing to do.
        if (rootBoneResolved && rootBoneForAnim == currentAnimation)
            return;

        rootBoneName.clear();
        rootBoneForAnim = currentAnimation;
        rootBoneResolved = true;
        rootMotionInitialized = false;

        if (!currentAnimation)
            return;

        // The root-motion bone is the topmost animated node in the hierarchy.
        const kNodeData &root = currentAnimation->getRootNode();
        findRootBoneRecursive(&root);
    }

    void kAnimator::findRootBoneRecursive(const kNodeData *node)
    {
        if (!node || !rootBoneName.empty())
            return;
        if (currentAnimation)
        {
            kBone *bone = currentAnimation->findBone(node->name);
            if (bone)
            {
                // Skip static container nodes. Many FBX exporters emit
                // constant (identity) animation channels on the armature's
                // parent node (e.g. "Skeleton"); the real root-motion bone is
                // the topmost node that actually moves (e.g. "Root" / "Hips").
                if (!bone->isStatic())
                {
                    rootBoneName = node->name;
                    return;
                }
            }
        }
        for (int i = 0; i < node->childrenCount; ++i)
            findRootBoneRecursive(&node->children[i]);
    }

    void kAnimator::handleRootMotion(kBone *bone, kMat4 &nodeTransform)
    {
        if (!currentAnimation || !bone)
            return;

        // Re-sample the root bone at the current time (the caller already
        // updated it, but resolveRootBone may have left it at a different time).
        bone->update(currentTime);
        const kMat4 currentLocal = bone->getLocalTransform();
        const kVec3  currentPos(currentLocal[3]);
        const kQuat  currentRot = kQuat(kMat3(currentLocal));

        // Loop / seek detection: when the clip time moved backwards since the
        // previous sample the clip wrapped to its start. Re-seed the tracker so
        // the end→start pose jump is never emitted as a huge delta that cancels
        // all the motion gathered over the previous loop (which snapped the
        // object back to its starting position).
        if (currentTime < lastRootTime)
            rootMotionInitialized = false;
        lastRootTime = currentTime;

        if (rootMotionInitialized)
        {
            // Per-frame delta accumulated for each enabled channel.
            if (currentAnimation->getRootMotionPositionXZ())
            {
                rootMotionAccumPos.x += currentPos.x - lastRootPos.x;
                rootMotionAccumPos.z += currentPos.z - lastRootPos.z;
            }
            if (currentAnimation->getRootMotionPositionY())
                rootMotionAccumPos.y += currentPos.y - lastRootPos.y;
            if (currentAnimation->getRootMotionRotation())
            {
                const kQuat dRot = currentRot * glm::conjugate(lastRootRot);
                rootMotionAccumRotEuler += glm::degrees(glm::eulerAngles(dRot));
            }
        }
        else
        {
            // First pose sampled: remember it as the baked reference so the
            // character stays exactly where it started, and seed the delta
            // tracker without producing a giant first-frame jump.
            bakeRootPos = currentPos;
            bakeRootRot = currentRot;
        }

        lastRootPos = currentPos;
        lastRootRot = currentRot;
        rootMotionInitialized = true;

        // Bake the enabled root-motion channels out of the pose, pinning them
        // to the start-of-play reference. Un-enabled channels keep animating.
        if (currentAnimation->getRootMotionPositionXZ())
        {
            nodeTransform[3][0] = bakeRootPos.x;
            nodeTransform[3][2] = bakeRootPos.z;
        }
        if (currentAnimation->getRootMotionPositionY())
            nodeTransform[3][1] = bakeRootPos.y;
        if (currentAnimation->getRootMotionRotation())
        {
            // Replace the rotation with the start rotation, preserving the
            // current translation and scale (root bones are usually rigid, but
            // keeping scale avoids degenerate matrices).
            const kVec3 scale(glm::length(kVec3(nodeTransform[0])),
                              glm::length(kVec3(nodeTransform[1])),
                              glm::length(kVec3(nodeTransform[2])));
            nodeTransform = glm::translate(kMat4(1.0f), kVec3(nodeTransform[3])) *
                            kMat4(bakeRootRot) *
                            glm::scale(kMat4(1.0f), scale);
        }
    }

    kVec3 kAnimator::getRootMotionDeltaPosition()
    {
        kVec3 delta = rootMotionAccumPos;
        rootMotionAccumPos = kVec3(0.0f);
        return delta;
    }

    kVec3 kAnimator::getRootMotionDeltaRotation()
    {
        kVec3 delta = rootMotionAccumRotEuler;
        rootMotionAccumRotEuler = kVec3(0.0f);
        return delta;
    }

    void kAnimator::resetRootMotion()
    {
        rootMotionAccumPos      = kVec3(0.0f);
        rootMotionAccumRotEuler = kVec3(0.0f);
        lastRootPos             = kVec3(0.0f);
        lastRootRot             = kQuat(1.0f, 0, 0, 0);
        lastRootTime            = 0.0f;
        rootMotionInitialized   = false;
        rootBoneResolved        = false;
        rootBoneForAnim         = nullptr;
        rootBoneName.clear();
    }

    bool kAnimator::getRootMotionPositionXZ() const
    {
        return currentAnimation ? currentAnimation->getRootMotionPositionXZ() : false;
    }

    bool kAnimator::getRootMotionPositionY() const
    {
        return currentAnimation ? currentAnimation->getRootMotionPositionY() : false;
    }

    bool kAnimator::getRootMotionRotation() const
    {
        return currentAnimation ? currentAnimation->getRootMotionRotation() : false;
    }
}
