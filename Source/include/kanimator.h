/**
 * @file kanimator.h
 * @brief Drives playback of skeletal (bone) animation, and reserves a hook
 *        for future non-skeletal (object-transform) animation.
 */

#ifndef KANIMATOR_H
#define KANIMATOR_H

#include "kdatatype.h"
#include "kskelanimation.h"
#include "kbone.h"
#include "kmesh.h"

#include <glm/gtx/string_cast.hpp>

namespace kemena
{
    class kSkeletalAnimation;
    class kAnimation;
    class kMesh;

    /**
     * @brief Controls playback of animation clips.
     *
     * Today the animator only plays skeletal clips (kSkeletalAnimation): it
     * produces a flat array of final bone matrices that the renderer uploads
     * to the shader each frame via @ref getFinalBoneMatrices().
     *
     * Non-skeletal (object-transform) clips of type kAnimation are accepted
     * through @ref setObjectAnimation() / @ref playObjectAnimation() but are
     * not yet executed — the hooks are in place for the future cinematic
     * editor; per-object track sampling will land alongside it.
     */
    class kAnimator
    {
    public:
        /**
         * @brief Constructs an animator and sets the initial skeletal clip.
         * @param newAnimation Skeletal animation to start playing.
         */
        kAnimator(kSkeletalAnimation *newAnimation);

        /** @brief Registers an additional skeletal clip. */
        void addAnimation(kSkeletalAnimation *newAnimation);

        /**
         * @brief Advances the active animation by one step.
         * @param newDeltaTime Elapsed time since the last update in seconds.
         * @param frameId      Frame identifier — skips duplicate updates.
         */
        void updateAnimation(float newDeltaTime, int frameId);

        /** @brief Switches to a different skeletal clip and resets time. */
        void playAnimation(kSkeletalAnimation *animation);

        /** @brief Currently active skeletal clip. */
        kSkeletalAnimation *getCurrentAnimation();

        /**
         * @brief Recursively computes bone transforms for the entire skeleton.
         * @param node            Current hierarchy node being processed.
         * @param parentTransform Accumulated world transform of the parent bone.
         */
        void calculateBoneTransform(const kNodeData *node, kMat4 parentTransform);

        /** @brief Per-bone world matrices ready for shader upload. */
        const std::vector<kMat4> getFinalBoneMatrices() const;

        /** @brief Seeks to a specific time in the active clip (ticks). */
        void setCurrentTime(float newTime);

        /** @brief Sets the global playback speed multiplier. */
        void setSpeed(float newSpeed);

        /** @brief Current global playback speed multiplier. */
        float getSpeed();

        // -------------------------------------------------------------------
        // Object-transform animation (kAnimation) — placeholder.
        //
        // Reserved for the future cinematic editor. The setters accept and
        // remember a clip but updateAnimation() doesn't drive any object
        // transforms yet; that pass will land when the editor is wired up.
        // -------------------------------------------------------------------

        /** @brief Registers the active non-skeletal clip (no-op playback for now). */
        void setObjectAnimation(kAnimation *clip);

        /** @brief Returns the registered non-skeletal clip, or nullptr. */
        kAnimation *getObjectAnimation() const;

    private:
        // Skeletal playback state.
        std::vector<kMat4>               finalBoneMatrices;             ///< Per-bone matrices.
        kSkeletalAnimation              *currentAnimation = nullptr;    ///< Active skeletal clip.
        std::vector<kSkeletalAnimation *> animations;                   ///< Registered skeletal clips.

        // Non-skeletal placeholder.
        kAnimation                      *objectAnimation = nullptr;       ///< Registered non-skeletal clip (not yet driven).

        // Playback time / pacing.
        float currentTime    = 0.0f;                                      ///< Current playback position in ticks.
        float deltaTime      = 0.0f;                                      ///< Last elapsed time in seconds.
        float speed          = 1.0f;                                      ///< Global playback speed multiplier.
        int   currentFrameId = -1;                                        ///< Last processed frame id (guards duplicate updates).
    };
}

#endif // KANIMATOR_H
