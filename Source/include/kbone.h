/**
 * @file kbone.h
 * @brief Single skeletal bone with position, rotation, and scale keyframe channels.
 */

#ifndef KBONE_H
#define KBONE_H

#include <string>
#include <stdexcept>
#include <iostream>

#include "kdatatype.h"

// Forward-declared so this header stays Assimp-free; the bone constructor
// takes an aiNodeAnim* only as an importer detail and the type is fully
// defined inside kbone.cpp where Assimp is included. Slim runtime builds
// (KEMENA_NO_ASSIMP) drop this entry point — animations in that build are
// populated by the tinygltf importer.
#ifndef KEMENA_NO_ASSIMP
struct aiNodeAnim;
#endif

namespace kemena
{
    /**
     * @brief Represents one bone in a skeletal animation.
     *
     * Constructed from an Assimp node animation channel.  Each frame, call
     * update() with the current animation time to interpolate between keyframes
     * and store the result in the local transform matrix, which is read by
     * kAnimator::calculateBoneTransform().
     */
    class kBone
    {
    public:
#ifndef KEMENA_NO_ASSIMP
        /**
         * @brief Constructs a bone from an Assimp animation channel.
         * @param boneName Human-readable bone name.
         * @param boneID   Index into the mesh's bone palette.
         * @param channel  Assimp node animation containing keyframe data.
         */
        kBone(const kString &boneName, int boneID, aiNodeAnim *channel);
#endif

        /**
         * @brief Constructs a bone from already-decoded keyframe arrays.
         *
         * Library-agnostic — populate the channel vectors from whatever
         * importer (Assimp / tinygltf / custom) sourced them.
         */
        kBone(const kString &boneName, int boneID,
              std::vector<kKeyPosition> positions,
              std::vector<kKeyRotation> rotations,
              std::vector<kKeyScale>    scales);

        /**
         * @brief Interpolates all channels and updates the local transform.
         * @param animationTime Current playback position in animation ticks.
         */
        void update(float animationTime);

        /**
         * @brief Returns the interpolated local transform for the current time.
         * @return 4x4 local-space bone transform matrix.
         */
        const kMat4 getLocalTransform() const;

        /**
         * @brief Returns the bone name.
         * @return Bone name as it appears in the asset.
         */
        const kString getName() const;

        /**
         * @brief Returns the bone palette index.
         * @return Zero-based index into the mesh's bone array.
         */
        const int getID() const;

        /**
         * @brief Finds the index of the position keyframe just before @p animationTime.
         * @param animationTime Current playback time in ticks.
         * @return Index of the preceding position keyframe.
         */
        int getPositionIndex(float animationTime);

        /**
         * @brief Finds the index of the rotation keyframe just before @p animationTime.
         * @param animationTime Current playback time in ticks.
         * @return Index of the preceding rotation keyframe.
         */
        int getRotationIndex(float animationTime);

        /**
         * @brief Finds the index of the scale keyframe just before @p animationTime.
         * @param animationTime Current playback time in ticks.
         * @return Index of the preceding scale keyframe.
         */
        int getScaleIndex(float animationTime);

    protected:
    private:
        std::vector<kKeyPosition> positions; ///< Position keyframe channel.
        std::vector<kKeyRotation> rotations; ///< Rotation keyframe channel.
        std::vector<kKeyScale>    scales;    ///< Scale keyframe channel.

        int positionCount; ///< Number of position keyframes.
        int rotationCount; ///< Number of rotation keyframes.
        int scaleCount;    ///< Number of scale keyframes.

        kMat4   localTransform = kMat4(1.0f); ///< Interpolated local transform.
        kString name;                        ///< Bone name.
        int    id;                          ///< Bone palette index.

        /**
         * @brief Computes the blend factor between two adjacent keyframes.
         * @param lastTimeStamp  Time of the earlier keyframe.
         * @param nextTimeStamp  Time of the later keyframe.
         * @param animationTime  Current playback time.
         * @param duration       Total animation duration in ticks.
         * @return Normalised blend factor in [0, 1].
         */
        float getScaleFactor(float lastTimeStamp, float nextTimeStamp, float animationTime, float duration);

        /**
         * @brief Interpolates between position keyframes and returns a translation matrix.
         * @param animationTime Current playback time in ticks.
         * @return 4x4 translation matrix.
         */
        kMat4 interpolatePosition(float animationTime);

        /**
         * @brief Interpolates between rotation keyframes and returns a rotation matrix.
         * @param animationTime Current playback time in ticks.
         * @return 4x4 rotation matrix.
         */
        kMat4 interpolateRotation(float animationTime);

        /**
         * @brief Interpolates between scale keyframes and returns a scale matrix.
         * @param animationTime Current playback time in ticks.
         * @return 4x4 scale matrix.
         */
        kMat4 interpolateScale(float animationTime);
    };
}

#endif // KBONE_H
