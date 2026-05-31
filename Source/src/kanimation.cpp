#include "kanimation.h"

#include <algorithm>

namespace kemena
{
    void kAnimation::addTrack(const kObjectAnimTrack &track)
    {
        tracks.push_back(track);
    }

    const std::vector<kObjectAnimTrack> &kAnimation::getTracks() const
    {
        return tracks;
    }

    const kObjectAnimTrack *kAnimation::findTrack(const kString &uuid) const
    {
        auto it = std::find_if(tracks.begin(), tracks.end(),
                               [&](const kObjectAnimTrack &t){ return t.targetUuid == uuid; });
        return it == tracks.end() ? nullptr : &(*it);
    }
}
