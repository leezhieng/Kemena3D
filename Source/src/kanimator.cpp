#include "kanimator.h"

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
            currentTime += currentAnimation->getTicksPerSecond() * newDeltaTime;
            currentTime = fmod(currentTime, currentAnimation->getDuration());

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
        }
    }

    kSkeletalAnimation *kAnimator::getCurrentAnimation()
    {
        return currentAnimation;
    }

    void kAnimator::calculateBoneTransform(const kNodeData *node, kMat4 parentTransform)
    {
        if (node == nullptr) return;

        kString nodeName      = node->name;
        kMat4   nodeTransform = node->transformation;

        kBone *bone = currentAnimation->findBone(nodeName);
        if (bone != nullptr)
        {
            bone->update(currentTime);
            nodeTransform = bone->getLocalTransform();
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
}
