#include "kbone.h"

#ifndef KEMENA_NO_ASSIMP
#include "kassimp_internal.h"
#include <assimp/anim.h>
#endif

namespace kemena
{
    kBone::kBone(const kString &boneName, int boneID,
                 std::vector<kKeyPosition> setPositions,
                 std::vector<kKeyRotation> setRotations,
                 std::vector<kKeyScale>    setScales)
    {
        name      = boneName;
        id        = boneID;
        positions = std::move(setPositions);
        rotations = std::move(setRotations);
        scales    = std::move(setScales);
        positionCount = (int)positions.size();
        rotationCount = (int)rotations.size();
        scaleCount    = (int)scales.size();
    }

#ifndef KEMENA_NO_ASSIMP
    kBone::kBone(const kString &boneName, int boneID, aiNodeAnim *channel)
    {
        name = boneName;
        id = boneID;

        positionCount = channel->mNumPositionKeys;
        for (int i = 0; i < positionCount; ++i)
        {
            kKeyPosition data;
            data.position  = kAssimpInternal::toVec3(channel->mPositionKeys[i].mValue);
            data.timeStamp = (float)channel->mPositionKeys[i].mTime;
            positions.push_back(data);
        }

        rotationCount = channel->mNumRotationKeys;
        for (int i = 0; i < rotationCount; ++i)
        {
            kKeyRotation data;
            data.orientation = kAssimpInternal::toQuat(channel->mRotationKeys[i].mValue);
            data.timeStamp   = (float)channel->mRotationKeys[i].mTime;
            rotations.push_back(data);
        }

        scaleCount = channel->mNumScalingKeys;
        for (int i = 0; i < scaleCount; ++i)
        {
            kKeyScale data;
            data.scale     = kAssimpInternal::toVec3(channel->mScalingKeys[i].mValue);
            data.timeStamp = (float)channel->mScalingKeys[i].mTime;
            scales.push_back(data);
        }
    }
#endif // KEMENA_NO_ASSIMP

    void kBone::update(float animationTime)
    {
        kMat4 translation = interpolatePosition(animationTime);
        kMat4 rotation = interpolateRotation(animationTime);
        kMat4 scale = interpolateScale(animationTime);
        localTransform = translation * rotation * scale;
    }

    const kMat4 kBone::getLocalTransform() const
    {
        return localTransform;
    }

    const kString kBone::getName() const
    {
        return name;
    }

    const int kBone::getID() const
    {
        return id;
    }

    int kBone::getPositionIndex(float animationTime)
    {
        // Handle empty positions array
        if (positions.empty())
        {
            throw std::runtime_error("No position keyframes found.");
        }

        // Handle animationTime before the first keyframe
        if (animationTime <= positions[0].timeStamp)
        {
            return 0;
        }

        // Handle animationTime after the last keyframe
        if (animationTime >= positions.back().timeStamp)
        {
            return static_cast<int>(positions.size()) - 1;
        }

        // Find the appropriate keyframe index
        for (int index = 0; index < static_cast<int>(positions.size()) - 1; index++)
        {
            if (animationTime < positions[index + 1].timeStamp)
                return index;
        }

        // This line should never be reached
        throw std::runtime_error("No valid position index found for animation time: " + std::to_string(animationTime));
    }

    int kBone::getRotationIndex(float animationTime)
    {
        // Handle empty rotations array
        if (rotations.empty())
        {
            throw std::runtime_error("No rotation keyframes found.");
        }

        // Handle animationTime before the first keyframe
        if (animationTime <= rotations[0].timeStamp)
        {
            return 0;
        }

        // Handle animationTime after the last keyframe
        if (animationTime >= rotations.back().timeStamp)
        {
            return static_cast<int>(rotations.size()) - 1;
        }

        // Find the appropriate keyframe index
        for (int index = 0; index < static_cast<int>(rotations.size()) - 1; index++)
        {
            if (animationTime < rotations[index + 1].timeStamp)
                return index;
        }

        // This line should never be reached
        throw std::runtime_error("No valid rotation index found for animation time: " + std::to_string(animationTime));
    }

    int kBone::getScaleIndex(float animationTime)
    {
        // Handle empty scales array
        if (scales.empty())
        {
            throw std::runtime_error("No scale keyframes found.");
        }

        // Handle animationTime before the first keyframe
        if (animationTime <= scales[0].timeStamp)
        {
            return 0;
        }

        // Handle animationTime after the last keyframe
        if (animationTime >= scales.back().timeStamp)
        {
            return static_cast<int>(scales.size()) - 1;
        }

        // Find the appropriate keyframe index
        for (int index = 0; index < static_cast<int>(scales.size()) - 1; index++)
        {
            if (animationTime < scales[index + 1].timeStamp)
                return index;
        }

        // This line should never be reached
        throw std::runtime_error("No valid scale index found for animation time: " + std::to_string(animationTime));
    }

    float kBone::getScaleFactor(float lastTimeStamp, float nextTimeStamp, float animationTime, float duration)
    {
        /*if (nextTimeStamp < lastTimeStamp)
            return 0.0f;

        float scaleFactor = 0.0f;
        float midWayLength = animationTime - lastTimeStamp;
        float framesDiff = nextTimeStamp - lastTimeStamp;
        scaleFactor = midWayLength / framesDiff;

        return scaleFactor;*/

        float framesDiff;
        float midWayLength;

        // Check for wrap-around: nextTimeStamp is less than lastTimeStamp.
        if (nextTimeStamp < lastTimeStamp)
        {
            framesDiff = (duration - lastTimeStamp) + nextTimeStamp;
            // If animationTime is before nextTimeStamp, it has wrapped.
            if (animationTime < nextTimeStamp)
                midWayLength = (duration - lastTimeStamp) + animationTime;
            else
                midWayLength = animationTime - lastTimeStamp;
        }
        else
        {
            framesDiff = nextTimeStamp - lastTimeStamp;
            midWayLength = animationTime - lastTimeStamp;
        }

        // Avoid division by zero:
        if (framesDiff < 0.0001f)
            return 0.0f;

        return midWayLength / framesDiff;
    }

    kMat4 kBone::interpolatePosition(float animationTime)
    {
        /*if (positionCount == 1)
            return glm::translate(kMat4(1.0f), positions[0].position);

        int p0Index = getPositionIndex(animationTime);
        int p1Index = p0Index + 1;

        // Handle looping case: if p1Index goes beyond last frame, snap to first frame
        if (p1Index >= positionCount)
            return glm::translate(kMat4(1.0f), positions[0].position);

        float scaleFactor = getScaleFactor(positions[p0Index].timeStamp, positions[p1Index].timeStamp, animationTime);
        kVec3 finalPosition = glm::mix(positions[p0Index].position, positions[p1Index].position, scaleFactor);
        return glm::translate(kMat4(1.0f), finalPosition);*/

        if (positionCount == 1)
            return glm::translate(kMat4(1.0f), positions[0].position);

        int p0Index = getPositionIndex(animationTime);
        int p1Index = p0Index + 1;

        // Get total animation duration
        float animationDuration = positions[positionCount - 1].timeStamp;

        // If at the last frame, wrap around to the first frame
        if (p1Index >= positionCount)
        {
            p1Index = 0;
            float lastTime = positions[p0Index].timeStamp;
            float firstTime = positions[p1Index].timeStamp + animationDuration; // Shift first frame forward
            float scaleFactor = getScaleFactor(lastTime, firstTime, animationTime, animationDuration);
            kVec3 finalPosition = glm::mix(positions[p0Index].position, positions[p1Index].position, scaleFactor);
            return glm::translate(kMat4(1.0f), finalPosition);
        }

        float scaleFactor = getScaleFactor(positions[p0Index].timeStamp, positions[p1Index].timeStamp, animationTime, animationDuration);
        kVec3 finalPosition = glm::mix(positions[p0Index].position, positions[p1Index].position, scaleFactor);
        return glm::translate(kMat4(1.0f), finalPosition);
    }

    kMat4 kBone::interpolateRotation(float animationTime)
    {
        /*if (rotationCount == 1)
        {
            auto rotation = glm::normalize(rotations[0].orientation);
            return glm::toMat4(rotation);
        }

        int p0Index = getRotationIndex(animationTime);
        int p1Index = p0Index + 1;

        // Handle looping case: if p1Index goes beyond last frame, snap to first frame
        if (p1Index >= rotationCount)
            return glm::toMat4(glm::normalize(rotations[0].orientation));

        float scaleFactor = getScaleFactor(rotations[p0Index].timeStamp, rotations[p1Index].timeStamp, animationTime);
        kQuat finalRotation = glm::slerp(rotations[p0Index].orientation, rotations[p1Index].orientation, scaleFactor);
        finalRotation = glm::normalize(finalRotation);
        return glm::toMat4(finalRotation);*/

        if (rotationCount == 1)
        {
            auto rotation = glm::normalize(rotations[0].orientation);
            return glm::toMat4(rotation);
        }

        int p0Index = getRotationIndex(animationTime);
        int p1Index = p0Index + 1;

        // Get total animation duration
        float animationDuration = rotations[rotationCount - 1].timeStamp;

        // If at the last frame, wrap around to the first frame
        if (p1Index >= rotationCount)
        {
            p1Index = 0;
            float lastTime = rotations[p0Index].timeStamp;
            float firstTime = rotations[p1Index].timeStamp + animationDuration; // Shift first frame forward
            float scaleFactor = getScaleFactor(lastTime, firstTime, animationTime, animationDuration);
            kQuat finalRotation = glm::slerp(rotations[p0Index].orientation, rotations[p1Index].orientation, scaleFactor);
            finalRotation = glm::normalize(finalRotation);
            return glm::toMat4(finalRotation);
        }

        float scaleFactor = getScaleFactor(rotations[p0Index].timeStamp, rotations[p1Index].timeStamp, animationTime, animationDuration);
        kQuat finalRotation = glm::slerp(rotations[p0Index].orientation, rotations[p1Index].orientation, scaleFactor);
        finalRotation = glm::normalize(finalRotation);
        return glm::toMat4(finalRotation);
    }

    kMat4 kBone::interpolateScale(float animationTime)
    {
        /*if (1 == scaleCount)
            return glm::scale(kMat4(1.0f), scales[0].scale);

        int p0Index = getScaleIndex(animationTime);
        int p1Index = p0Index + 1;

        // Handle looping case: if p1Index goes beyond last frame, snap to first frame
        if (p1Index >= scaleCount)
            return glm::scale(kMat4(1.0f), scales[0].scale);

        float scaleFactor = getScaleFactor(scales[p0Index].timeStamp, scales[p1Index].timeStamp, animationTime);
        kVec3 finalScale = glm::mix(scales[p0Index].scale, scales[p1Index].scale, scaleFactor);
        return glm::scale(kMat4(1.0f), finalScale);*/

        if (scaleCount == 1)
            return glm::scale(kMat4(1.0f), scales[0].scale);

        int p0Index = getScaleIndex(animationTime);
        int p1Index = p0Index + 1;

        // Get total animation duration
        float animationDuration = scales[scaleCount - 1].timeStamp;

        // If at the last frame, wrap around to the first frame
        if (p1Index >= scaleCount)
        {
            p1Index = 0;
            float lastTime = scales[p0Index].timeStamp;
            float firstTime = scales[p1Index].timeStamp + animationDuration; // Shift first frame forward
            float scaleFactor = getScaleFactor(lastTime, firstTime, animationTime, animationDuration);
            kVec3 finalScale = glm::mix(scales[p0Index].scale, scales[p1Index].scale, scaleFactor);
            return glm::scale(kMat4(1.0f), finalScale);
        }

        float scaleFactor = getScaleFactor(scales[p0Index].timeStamp, scales[p1Index].timeStamp, animationTime, animationDuration);
        kVec3 finalScale = glm::mix(scales[p0Index].scale, scales[p1Index].scale, scaleFactor);
        return glm::scale(kMat4(1.0f), finalScale);
    }
}
