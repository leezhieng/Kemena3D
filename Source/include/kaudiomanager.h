/**
 * @file kaudiomanager.h
 * @brief Audio subsystem manager — initialises the audio engine and owns audio clips.
 */

#ifndef KAUDIOMANAGER_H
#define KAUDIOMANAGER_H

#include "kexport.h"
#include "kdatatype.h"
#include "kaudio.h"

#include <vector>

namespace kemena
{
    /**
     * @brief Owns the miniaudio engine and acts as a factory for kAudio instances.
     *
     * Create one kAudioManager per application.  Call init() before loading any
     * audio, and shutdown() (or let the destructor do it) when done.
     *
     * @code
     *   kAudioManager* audio = new kAudioManager();
     *   audio->init();
     *
     *   kAudio* music = audio->loadAudio("Assets/audio/music.ogg");
     *   music->setLooping(true);
     *   music->play();
     *
     *   // Set listener position each frame
     *   audio->setListenerPosition(camera->getPosition());
     *   audio->setListenerDirection(camera->getForward(), camera->getUp());
     * @endcode
     */
    class KEMENA3D_API kAudioManager
    {
    public:
        /** @brief Constructs the manager; call init() before use. */
        kAudioManager();

        /** @brief Destroys the manager, calling shutdown() if still active. */
        ~kAudioManager();

        // --- Lifecycle -------------------------------------------------------

        /**
         * @brief Initialises the underlying miniaudio engine.
         * @return true on success; false if the audio device could not be opened.
         */
        bool init();

        /**
         * @brief Stops all sounds, unloads every kAudio, and tears down the engine.
         * Called automatically by the destructor.
         */
        void shutdown();

        // --- Global controls -------------------------------------------------

        /**
         * @brief Sets the master output volume.
         * @param volume Linear scalar; 0.0 = mute, 1.0 = full volume.
         */
        void setMasterVolume(float volume);

        /** @brief Returns the current master volume. */
        float getMasterVolume() const;

        // --- 3D listener -----------------------------------------------------

        /**
         * @brief Moves the audio listener to a new world-space position.
         * @param position Listener position (typically the camera position).
         */
        void setListenerPosition(const kVec3 &position);

        /**
         * @brief Sets the listener's orientation.
         * @param forward Normalised forward vector.
         * @param up      Normalised up vector.
         */
        void setListenerDirection(const kVec3 &forward, const kVec3 &up);

        /**
         * @brief Sets the listener's velocity for Doppler shift calculation.
         * @param velocity Velocity in world units per second.
         */
        void setListenerVelocity(const kVec3 &velocity);

        // --- Audio clip factory ----------------------------------------------

        /**
         * @brief Loads an audio file and returns a new kAudio instance.
         *
         * The returned pointer is owned by this manager.  Release it with
         * unloadAudio() rather than deleting it directly.
         *
         * Supports any format miniaudio accepts: WAV, MP3, FLAC, OGG (via stb_vorbis).
         *
         * @param filePath Path to the audio file.
         * @return Pointer to the loaded kAudio, or nullptr on failure.
         */
        kAudio *loadAudio(const kString &filePath);

        /**
         * @brief Stops and destroys a previously loaded kAudio.
         * @param audio Pointer returned by loadAudio().
         */
        void unloadAudio(kAudio *audio);

    protected:
    private:
        struct Impl;
        Impl *m_impl;
    };

} // namespace kemena

#endif // KAUDIOMANAGER_H
