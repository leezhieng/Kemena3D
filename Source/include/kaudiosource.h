/**
 * @file kaudiosource.h
 * @brief Audio emitter and listener component descriptors for scene objects.
 */

#ifndef KAUDIOSOURCE_H
#define KAUDIOSOURCE_H

#include "kexport.h"
#include "kdatatype.h"

namespace kemena
{
    /**
     * @brief Describes an audio emitter attached to a scene object.
     *
     * At runtime kAudioManager loads the referenced file and positions
     * the sound at the owning object's world-space position each frame.
     */
    struct KEMENA3D_API kAudioSource
    {
        kString uuid;                ///< Unique identifier of this audio source.
        kString name        = "Audio Source"; ///< Human-readable name shown in the editor.
        kString audioFile   = "";    ///< Path to the audio clip (WAV / MP3 / OGG / FLAC).
        bool    isActive    = true;  ///< When false, the source is ignored by the audio manager.
        bool    playOnAwake = false; ///< If true, playback starts automatically when the scene begins.
        bool    loop        = false; ///< If true, the clip repeats continuously.
        float   volume      = 1.0f;  ///< 0 = silent, 1 = full volume.
        float   pitch       = 1.0f;  ///< 1 = normal speed/pitch.
        bool    spatialize  = true;  ///< false = 2-D (no panning/attenuation).
        float   minDistance = 1.0f;  ///< Distance at which attenuation begins.
        float   maxDistance = 100.0f;///< Distance beyond which the sound is inaudible.
    };

    /**
     * @brief Marks a scene object as the audio listener (typically the main camera).
     *
     * The runtime queries objects carrying a kAudioListener each frame and
     * forwards their position and orientation to kAudioManager::setListenerPosition().
     * Only one active listener per scene is meaningful.
     */
    struct KEMENA3D_API kAudioListener
    {
        kString uuid;               ///< Unique identifier of this listener.
        bool    isActive = true;    ///< When false, this object is not used as the active listener.
    };

} // namespace kemena

#endif // KAUDIOSOURCE_H
