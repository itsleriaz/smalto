#include "core/Playback.h"

#include <algorithm>

namespace core {

PlaybackCursor::PlaybackCursor(int first, int last, AniDirection direction, int repeat, int startFrame)
    : m_first(std::min(first, last))
    , m_last(std::max(first, last))
    , m_frame(std::clamp(startFrame, m_first, m_last))
    , m_step(direction == AniDirection::Reverse || direction == AniDirection::PingPongReverse ? -1 : 1)
    , m_pingPong(direction == AniDirection::PingPong || direction == AniDirection::PingPongReverse)
    , m_passesLeft(std::max(0, repeat))
{
}

std::optional<int> PlaybackCursor::next()
{
    const int end = m_step > 0 ? m_last : m_first;
    if (m_frame != end) {
        m_frame += m_step;
        return m_frame;
    }
    // A pass just finished.
    if (m_passesLeft > 0 && --m_passesLeft == 0)
        return std::nullopt;
    if (m_pingPong && m_first != m_last) {
        m_step = -m_step;
        m_frame += m_step;
    } else {
        m_frame = m_step > 0 ? m_first : m_last;
    }
    return m_frame;
}

int tagAt(const Document& doc, int frame)
{
    int best = -1;
    for (int i = 0; i < int(doc.tags.size()); ++i) {
        const Tag& tag = doc.tags[size_t(i)];
        if (frame < tag.from || frame > tag.to)
            continue;
        if (best < 0 || tag.to - tag.from < doc.tags[size_t(best)].to - doc.tags[size_t(best)].from)
            best = i;
    }
    return best;
}

} // namespace core
