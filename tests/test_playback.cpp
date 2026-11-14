#include <doctest/doctest.h>

#include "core/Playback.h"

#include <vector>

using namespace core;

namespace {

// The next `n` frames (stops early when the cursor finishes; -1 marks it).
std::vector<int> play(PlaybackCursor cursor, int n)
{
    std::vector<int> out;
    for (int i = 0; i < n; ++i) {
        const auto f = cursor.next();
        out.push_back(f ? *f : -1);
        if (!f)
            break;
    }
    return out;
}

} // namespace

TEST_CASE("playback: forward and reverse loop")
{
    CHECK(play(PlaybackCursor(2, 4, AniDirection::Forward, 0, 2), 6) == std::vector{3, 4, 2, 3, 4, 2});
    CHECK(play(PlaybackCursor(2, 4, AniDirection::Reverse, 0, 4), 6) == std::vector{3, 2, 4, 3, 2, 4});
    // Starting mid-tag continues from there.
    CHECK(play(PlaybackCursor(0, 3, AniDirection::Forward, 0, 2), 3) == std::vector{3, 0, 1});
    // Outside the range: clamped in.
    CHECK(PlaybackCursor(2, 4, AniDirection::Forward, 0, 9).frame() == 4);
}

TEST_CASE("playback: ping-pong turns without repeating the ends")
{
    CHECK(play(PlaybackCursor(0, 3, AniDirection::PingPong, 0, 0), 8) == std::vector{1, 2, 3, 2, 1, 0, 1, 2});
    CHECK(play(PlaybackCursor(0, 2, AniDirection::PingPongReverse, 0, 2), 5) == std::vector{1, 0, 1, 2, 1});
    CHECK(play(PlaybackCursor(5, 5, AniDirection::PingPong, 0, 5), 3) == std::vector{5, 5, 5});
}

TEST_CASE("playback: repeat counts passes, then stops")
{
    // Two forward passes from the start: 0 1 2 | 0 1 2, then done.
    CHECK(play(PlaybackCursor(0, 2, AniDirection::Forward, 2, 0), 8) == std::vector{1, 2, 0, 1, 2, -1});
    // Ping-pong, repeat 2: there and back once.
    CHECK(play(PlaybackCursor(0, 2, AniDirection::PingPong, 2, 0), 8) == std::vector{1, 2, 1, 0, -1});
    CHECK(play(PlaybackCursor(0, 0, AniDirection::Forward, 1, 0), 2) == std::vector{-1});
}

TEST_CASE("playback: the innermost tag under the playhead")
{
    Document doc(1, 1);
    for (int f = 1; f < 10; ++f)
        doc.insertFrame(f, Frame{});
    doc.tags = {{"all", 0, 9, {}, 0, {}, {}}, {"walk", 2, 5, {}, 0, {}, {}}, {"step", 4, 4, {}, 0, {}, {}}};
    CHECK(tagAt(doc, 0) == 0);
    CHECK(tagAt(doc, 3) == 1);
    CHECK(tagAt(doc, 4) == 2);
    doc.tags.erase(doc.tags.begin());
    CHECK(tagAt(doc, 8) == -1);
}
