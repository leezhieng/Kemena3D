/**
 * @file kanimation.h
 * @brief Non-skeletal (object-transform) animation clip.
 *
 * For skeletal / bone animation see kSkeletalAnimation in kskelanimation.h.
 *
 * This class is the stub for a future cinematic / scene-animation editor:
 * it stores per-object transform tracks (position / rotation / scale
 * keyframes keyed by target object UUID) and is played back by kAnimator.
 * The API surface is intentionally minimal for now — fill it in when the
 * editor lands.
 */

#ifndef KANIMATION_H
#define KANIMATION_H

#include "kdatatype.h"

#include <vector>
#include <map>

namespace kemena
{
    /**
     * @brief A single transform track for one target object inside a clip.
     *
     * The three channels (position / rotation / scale) are sparse —
     * keyframes are stored individually and the animator interpolates
     * between them.
     */
    struct kObjectAnimTrack
    {
        kString                   targetUuid; ///< kObject UUID the track drives.
        std::vector<kKeyPosition> positions;  ///< Position channel keyframes.
        std::vector<kKeyRotation> rotations;  ///< Rotation channel keyframes.
        std::vector<kKeyScale>    scales;     ///< Scale channel keyframes.
    };

    /**
     * @brief Non-skeletal animation clip — collection of per-object tracks.
     *
     * Reserved for the future cinematic editor. Construct empty, add tracks
     * via addTrack(), set the duration / tick rate, then hand to a kAnimator
     * for playback. Loading from disk will come once a clip file format is
     * defined.
     */
    class kAnimation
    {
    public:
        /** @brief Construct an empty clip with default duration / tick rate. */
        kAnimation() = default;

        /** @brief Append a transform track for one target object. */
        void addTrack(const kObjectAnimTrack &track);

        /** @brief All tracks in this clip. */
        const std::vector<kObjectAnimTrack> &getTracks() const;

        /** @brief Find a track by its target UUID, or nullptr if absent. */
        const kObjectAnimTrack *findTrack(const kString &uuid) const;

        /**
         * @brief Set the clip name.
         * @param n New clip name.
         */
        void  setName(const kString &n)          { name = n; }
        /**
         * @brief Get the clip name.
         * @return The clip name.
         */
        kString getName() const                  { return name; }

        /**
         * @brief Set the total clip length in ticks.
         * @param d Duration in ticks.
         */
        void  setDuration(float d)               { duration = d; }
        /**
         * @brief Get the total clip length in ticks.
         * @return Duration in ticks.
         */
        float getDuration() const                { return duration; }

        /**
         * @brief Set the tick rate used to convert ticks to seconds.
         * @param t Ticks per second.
         */
        void  setTicksPerSecond(float t)         { ticksPerSecond = t; }
        /**
         * @brief Get the tick rate.
         * @return Ticks per second.
         */
        float getTicksPerSecond() const          { return ticksPerSecond; }

        /**
         * @brief Set the playback speed multiplier.
         * @param s Speed multiplier (1.0 = normal speed).
         */
        void  setSpeed(float s)                  { speed = s; }
        /**
         * @brief Get the playback speed multiplier.
         * @return Speed multiplier (1.0 = normal speed).
         */
        float getSpeed() const                   { return speed; }

    private:
        kString name;                              ///< Clip name.
        float   duration       = 0.0f;             ///< Total length in ticks.
        float   ticksPerSecond = 25.0f;            ///< Tick rate.
        float   speed          = 1.0f;             ///< Playback speed multiplier.
        std::vector<kObjectAnimTrack> tracks;      ///< Per-target transform tracks.
    };
}

#endif // KANIMATION_H
