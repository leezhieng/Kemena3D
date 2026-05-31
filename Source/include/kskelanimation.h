/**
 * @file kskelanimation.h
 * @brief Skeletal animation clip — bone keyframes + node hierarchy.
 *
 * This is THE skeletal-animation class. Construct one from an asset file
 * (kSkeletalAnimation(path, mesh)) or via kAssetManager::loadAnimation();
 * a kAnimator then plays it back on the mesh's bone palette each frame.
 *
 * For non-skeletal (object-transform) animation see kAnimation — that class
 * is reserved for the future cinematic / scene-animation editor.
 */

#ifndef KSKELANIMATION_H
#define KSKELANIMATION_H

#include "kexport.h"
#include "kdatatype.h"
#include "kbone.h"

#include <map>
#include <vector>

namespace kemena
{
    class kMesh;

    /**
     * @brief A single skeletal animation clip (bone channels + node hierarchy).
     *
     * Loaded via kAssetManager::loadAnimation(). The clip stores a list of
     * kBone objects (one per animated joint) and the root of the bone
     * hierarchy (kNodeData) used by kAnimator to propagate transforms.
     *
     * Playback speed can be scaled with setSpeed(); the kAnimator queries
     * getDuration() and getTicksPerSecond() to advance time correctly.
     */
    class KEMENA3D_API kSkeletalAnimation
    {
    public:
#ifndef KEMENA_NO_ASSIMP
        /**
         * @brief Loads an animation clip from an asset file (Assimp) and binds
         *        it to a mesh.
         * @param animationPath Path to the animation asset.
         * @param setMesh       Mesh whose bone map resolves bone indices.
         *
         * Available only in editor-style builds. Slim runtime builds load
         * glTF animations through the tinygltf path on kAssetManager.
         */
        kSkeletalAnimation(const kString &animationPath, kMesh *setMesh);
#endif

        /** @brief Default constructor — leaves the clip empty until populated. */
        kSkeletalAnimation();

        /** @brief Finds the kBone with the given name in this clip. */
        kBone *findBone(const kString &name);

        /** @brief Tick rate of this animation (as specified in the asset). */
        float getTicksPerSecond() const;

        /** @brief Total duration of this animation in ticks. */
        float getDuration() const;

        /** @brief Root of the bone hierarchy. */
        const kNodeData &getRootNode() const;

        /** @brief All meshes this animation has been bound to. */
        std::vector<kMesh *> getMeshes();

        /** @brief Set the playback speed multiplier (1.0 = normal). */
        void setSpeed(float newSpeed);

        /** @brief Current playback speed multiplier. */
        float getSpeed() const;

    private:
        float                        duration       = 0.0f; ///< Total length in ticks.
        int                          ticksPerSecond = 25;   ///< Tick rate.
        std::vector<kBone>           bones;                 ///< Per-bone channel data.
        kNodeData                    rootNode;              ///< Root of the node hierarchy.
        std::vector<kMesh *>         meshes;                ///< Bound mesh references.
        std::map<kString, kBoneInfo> boneInfoMap;           ///< Bone name → info lookup.
        float                        speed = 1.0f;          ///< Playback speed multiplier.
    };
}

#endif // KSKELANIMATION_H
