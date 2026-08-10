#include "kaudio.h"

// miniaudio.h included WITHOUT MINIAUDIO_IMPLEMENTATION here;
// the implementation is defined once in kaudiomanager.cpp.
#include "miniaudio.h"

#include <iostream>

namespace kemena
{
    struct kAudio::Impl
    {
        ma_sound sound       = {};
        bool     initialized = false;
    };

    kAudio::kAudio()
        : m_impl(new Impl())
    {
    }

    kAudio::~kAudio()
    {
        uninit();
        delete m_impl;
    }

    bool kAudio::initFromFile(void *maEngine, const kString &filePath)
    {
        if (m_impl->initialized)
            uninit();

        ma_engine *engine = static_cast<ma_engine *>(maEngine);

        // Synchronous decode — avoids the race where ma_sound_start is called
        // before the async worker has populated the data source, which causes
        // the sound to immediately hit end-of-stream and never produce audio.
        ma_result result = ma_sound_init_from_file(engine,
                                                    filePath.c_str(),
                                                    MA_SOUND_FLAG_DECODE,
                                                    nullptr, nullptr,
                                                    &m_impl->sound);
        if (result != MA_SUCCESS)
        {
            std::cout << "[kAudio] Failed to load '" << filePath
                      << "' (ma_result = " << result << ")." << std::endl;
            return false;
        }

        m_impl->initialized = true;
        return true;
    }

    void kAudio::uninit()
    {
        if (m_impl->initialized)
        {
            ma_sound_uninit(&m_impl->sound);
            m_impl->initialized = false;
        }
    }

    // --- Playback controls ---------------------------------------------------

    void kAudio::play()
    {
        if (m_impl->initialized)
            ma_sound_start(&m_impl->sound);
    }

    void kAudio::stop()
    {
        if (m_impl->initialized)
        {
            ma_sound_stop(&m_impl->sound);
            ma_sound_seek_to_pcm_frame(&m_impl->sound, 0);
        }
    }

    void kAudio::pause()
    {
        if (m_impl->initialized)
            ma_sound_stop(&m_impl->sound);
    }

    void kAudio::resume()
    {
        if (m_impl->initialized)
            ma_sound_start(&m_impl->sound);
    }

    // --- Properties ----------------------------------------------------------

    void kAudio::setLooping(bool loop)
    {
        if (m_impl->initialized)
            ma_sound_set_looping(&m_impl->sound, loop ? MA_TRUE : MA_FALSE);
    }

    void kAudio::setVolume(float volume)
    {
        if (m_impl->initialized)
            ma_sound_set_volume(&m_impl->sound, volume);
    }

    void kAudio::setPitch(float pitch)
    {
        if (m_impl->initialized)
            ma_sound_set_pitch(&m_impl->sound, pitch);
    }

    void kAudio::setPosition(const kVec3 &position)
    {
        if (m_impl->initialized)
            ma_sound_set_position(&m_impl->sound, position.x, position.y, position.z);
    }

    void kAudio::setVelocity(const kVec3 &velocity)
    {
        if (m_impl->initialized)
            ma_sound_set_velocity(&m_impl->sound, velocity.x, velocity.y, velocity.z);
    }

    void kAudio::setSpatialization(bool enable)
    {
        if (m_impl->initialized)
            ma_sound_set_spatialization_enabled(&m_impl->sound, enable ? MA_TRUE : MA_FALSE);
    }

    void kAudio::setMinDistance(float distance)
    {
        if (m_impl->initialized)
            ma_sound_set_min_distance(&m_impl->sound, distance);
    }

    void kAudio::setMaxDistance(float distance)
    {
        if (m_impl->initialized)
            ma_sound_set_max_distance(&m_impl->sound, distance);
    }

    void kAudio::setAttenuationModel(int model)
    {
        if (m_impl->initialized)
            ma_sound_set_attenuation_model(&m_impl->sound,
                                           static_cast<ma_attenuation_model>(model));
    }

    void kAudio::setRolloff(float rolloff)
    {
        if (m_impl->initialized)
            ma_sound_set_rolloff(&m_impl->sound, rolloff);
    }

    // --- State queries -------------------------------------------------------

    bool kAudio::isPlaying() const
    {
        return m_impl->initialized && ma_sound_is_playing(&m_impl->sound) == MA_TRUE;
    }

    bool kAudio::isPaused() const
    {
        if (!m_impl->initialized)
            return false;
        // miniaudio has no explicit "paused" state — stopped without seeking = paused
        return !ma_sound_is_playing(&m_impl->sound) &&
               !ma_sound_at_end(&m_impl->sound);
    }

    bool kAudio::isLooping() const
    {
        return m_impl->initialized && ma_sound_is_looping(&m_impl->sound) == MA_TRUE;
    }

} // namespace kemena
