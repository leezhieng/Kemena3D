/**
 * @file kaudio.h
 * @brief Single audio clip — 2D or 3D positional playback.
 */

#ifndef KAUDIO_H
#define KAUDIO_H

#include "kexport.h"
#include "kdatatype.h"

namespace kemena
{
    /**
     * @brief Represents a loaded audio clip that can be played, stopped, and positioned.
     *
     * Instances must be created through kAudioManager::loadAudio() and destroyed with
     * kAudioManager::unloadAudio() — do not instantiate directly.
     *
     * @par 2D audio (default)
     * @code
     *   kAudio* sfx = audioManager->loadAudio("Assets/audio/explosion.wav");
     *   sfx->setVolume(0.8f);
     *   sfx->play();
     * @endcode
     *
     * @par 3D positional audio
     * @code
     *   kAudio* footstep = audioManager->loadAudio("Assets/audio/footstep.wav");
     *   footstep->setPosition(kVec3(10.f, 0.f, -5.f));
     *   footstep->setLooping(true);
     *   footstep->play();
     * @endcode
     */
    class KEMENA3D_API kAudio
    {
    public:
        /** @brief Constructs an empty audio clip; call initFromFile() (via kAudioManager) before use. */
        kAudio();

        /** @brief Destructor; releases the underlying miniaudio sound resource. */
        ~kAudio();

        // --- Playback controls -----------------------------------------------

        /** @brief Starts or resumes playback from the current position. */
        void play();

        /** @brief Stops playback and resets the cursor to the beginning. */
        void stop();

        /** @brief Pauses playback without resetting the cursor. */
        void pause();

        /** @brief Resumes a paused sound. Equivalent to play() if already stopped. */
        void resume();

        // --- Properties ------------------------------------------------------

        /**
         * @brief Sets the looping flag.
         * @param loop true to loop indefinitely.
         */
        void setLooping(bool loop);

        /**
         * @brief Sets the playback volume.
         * @param volume Linear amplitude scalar; 0.0 = silent, 1.0 = full volume.
         */
        void setVolume(float volume);

        /**
         * @brief Sets the playback pitch / speed multiplier.
         * @param pitch 1.0 = normal speed; >1.0 = higher pitch and faster.
         */
        void setPitch(float pitch);

        /**
         * @brief Positions the sound in 3D world space for spatialized audio.
         * @param position World-space position of the audio emitter.
         */
        void setPosition(const kVec3 &position);

        /**
         * @brief Sets the velocity of the audio emitter (used for Doppler shift).
         * @param velocity Velocity vector in world units per second.
         */
        void setVelocity(const kVec3 &velocity);

        /**
         * @brief Enables or disables 3D spatialization for this sound.
         * @param enable false = 2D (no panning or attenuation); true = 3D (default for loaded sounds).
         */
        void setSpatialization(bool enable);

        // --- State queries ---------------------------------------------------

        /** @brief Returns true if the sound is currently playing. */
        bool isPlaying() const;

        /** @brief Returns true if the sound is paused. */
        bool isPaused() const;

        /** @brief Returns true if the looping flag is set. */
        bool isLooping() const;

        // --- Internal init (called by kAudioManager) -------------------------

        /**
         * @brief Initialises the sound from a file using an existing miniaudio engine.
         * @param maEngine Opaque pointer to a `ma_engine` instance owned by kAudioManager.
         * @param filePath Path to the audio file (WAV, MP3, FLAC, OGG, …).
         * @return true on success.
         * @note Not part of the public API — call kAudioManager::loadAudio() instead.
         */
        bool initFromFile(void *maEngine, const kString &filePath);

        /** @brief Releases the miniaudio sound resource. */
        void uninit();

    protected:
    private:
        struct Impl;
        Impl *m_impl;
    };

} // namespace kemena

#endif // KAUDIO_H
