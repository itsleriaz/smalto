#pragma once

#include "core/Document.h"

#include <optional>

namespace core {

// Steps through frames [first, last] the way an animation tag plays:
// forward, reverse, or ping-pong (endpoints not repeated at the turns).
// Each one-way pass counts as one repeat; repeat 0 loops forever.
class PlaybackCursor {
public:
    PlaybackCursor(int first, int last, AniDirection direction, int repeat, int startFrame);

    int frame() const { return m_frame; }
    // The frame after this one, or nullopt once the last repeat has played.
    std::optional<int> next();

private:
    int m_first;
    int m_last;
    int m_frame;
    int m_step;       // +1 or -1
    bool m_pingPong;
    int m_passesLeft; // 0 = forever
};

// The tag playback should loop when starting at `frame`: the shortest tag
// containing it, or -1 for none.
int tagAt(const Document& doc, int frame);

} // namespace core
