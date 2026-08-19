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
#include <unordered_map>

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

        /** @brief Number of registered skeletal clips. */
        int getAnimationCount() const { return (int)animations.size(); }

        /** @brief Registered skeletal clip by index, or nullptr. */
        kSkeletalAnimation *getAnimation(int index) const
        {
            return (index >= 0 && index < (int)animations.size()) ? animations[index] : nullptr;
        }

        /** @brief Plays a registered clip by index and resets time. */
        void playAnimation(int index);

        /** @brief Current playback position in ticks. */
        float getCurrentTime() const { return currentTime; }

        // --- Named controller variables -------------------------------------
        // The editor animator controller drives state transitions from named
        // variables (bool / float / int / trigger). Scripts can set them through
        // these methods; the editor samples getVariables() each step.

        /** @brief Raw variable access used by the editor controller. */
        void  setVariable(const kString &name, float value) { variables[name] = value; }

        /** @brief Reads a variable value (0.0f when absent). */
        float getVariable(const kString &name) const
        {
            auto it = variables.find(name);
            return it != variables.end() ? it->second : 0.0f;
        }

        /** @brief All named controller variables (editor transition evaluation). */
        const std::unordered_map<kString, float> &getVariables() const { return variables; }

        /** @brief Sets a bool variable (stored as 0/1). */
        void setBool(const kString &name, bool value)     { variables[name] = value ? 1.0f : 0.0f; }

        /** @brief Sets a float variable. */
        void setFloat(const kString &name, float value)   { variables[name] = value; }

        /** @brief Sets an integer variable (stored as a float). */
        void setInt(const kString &name, int value)       { variables[name] = (float)value; }

        /** @brief Fires a trigger variable (sets it to 1.0f). */
        void setTrigger(const kString &name)              { variables[name] = 1.0f; }

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

        // Named controller variables (bool/float/int/trigger stored as floats).
        std::unordered_map<kString, float> variables;                     ///< Controller variable values.

        // Playback time / pacing.
        float currentTime    = 0.0f;                                      ///< Current playback position in ticks.
        float deltaTime      = 0.0f;                                      ///< Last elapsed time in seconds.
        float speed          = 1.0f;                                      ///< Global playback speed multiplier.
        int   currentFrameId = -1;                                        ///< Last processed frame id (guards duplicate updates).
    };
}

#endif // KANIMATOR_H
