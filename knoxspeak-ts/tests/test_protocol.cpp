#include "core/protocol.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace knoxspeak::proto;

TEST_CASE("parses HELLO") {
    auto m = parseGameMessage("HELLO\t1\t0.1.0\tLulu");
    REQUIRE(m);
    auto& h = std::get<Hello>(*m);
    CHECK(h.version == 1);
    CHECK(h.modVersion == "0.1.0");
    CHECK(h.username == "Lulu");
}

TEST_CASE("parses SPK and clamps ranges") {
    auto m = parseGameMessage("SPK\tR10TGRRL J4N3\t1.5\t-0.2\t-3\r");
    REQUIRE(m);
    auto& s = std::get<Speaker>(*m);
    CHECK(s.username == "R10TGRRL J4N3");
    CHECK(s.params.gain == Catch::Approx(1.0f));
    CHECK(s.params.muffle == Catch::Approx(0.0f));
    CHECK(s.params.pan == Catch::Approx(-1.0f));
}

TEST_CASE("mic mute messages") {
    auto on = parseGameMessage("MUTE\t1");
    REQUIRE(on);
    CHECK(std::get<SetMicMuted>(*on).muted);
    CHECK_FALSE(std::get<SetMicMuted>(*parseGameMessage("MUTE\t0")).muted);
    CHECK_FALSE(parseGameMessage("MUTE\tyes"));
    CHECK_FALSE(parseGameMessage("MUTE"));
    CHECK(encodeMic(true) == "MIC\t1\n");
}

TEST_CASE("rejects malformed and unknown lines") {
    CHECK_FALSE(parseGameMessage(""));
    CHECK_FALSE(parseGameMessage("NOPE\t1"));
    CHECK_FALSE(parseGameMessage("SPK\tRadio\tabc\t0\t0"));
    CHECK_FALSE(parseGameMessage("SPK\tRadio\tnan\t0\t0"));
    CHECK_FALSE(parseGameMessage("SPK\t\t1\t0\t0"));
    CHECK_FALSE(parseGameMessage("FRAME\t-1"));
    CHECK_FALSE(parseGameMessage("FRAME\t12x"));
}

TEST_CASE("frame assembler commits only matching seq") {
    FrameAssembler fa;
    CHECK_FALSE(fa.feed(FrameBegin{7}));
    CHECK_FALSE(fa.feed(Speaker{"  Doc ", {0.5f, 0.25f, 0.1f}}));
    auto frame = fa.feed(FrameEnd{7});
    REQUIRE(frame);
    REQUIRE(frame->contains("doc"));
    CHECK(frame->at("doc").gain == Catch::Approx(0.5f));

    CHECK_FALSE(fa.feed(FrameBegin{8}));
    CHECK_FALSE(fa.feed(Speaker{"Doc", {1, 0, 0}}));
    CHECK_FALSE(fa.feed(FrameEnd{9})); // mismatched: dropped
    CHECK_FALSE(fa.feed(FrameEnd{8})); // no open frame any more
}

TEST_CASE("speakers outside a frame are ignored") {
    FrameAssembler fa;
    CHECK_FALSE(fa.feed(Speaker{"Denise", {1, 0, 0}}));
    fa.feed(FrameBegin{1});
    auto frame = fa.feed(FrameEnd{1});
    REQUIRE(frame);
    CHECK(frame->empty());
}

TEST_CASE("line splitter handles partial reads and limits") {
    LineSplitter sp;
    std::vector<std::string> lines;
    auto collect = [&](std::string_view l) { lines.emplace_back(l); };
    CHECK(sp.append("PI", collect));
    CHECK(sp.append("NG\tx\nBY", collect));
    CHECK(sp.append("E\n", collect));
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "PING\tx");
    CHECK(lines[1] == "BYE");

    CHECK_FALSE(sp.append(std::string(kMaxLineBytes + 1, 'a'), collect));
}

TEST_CASE("encoders sanitize fields") {
    CHECK(encodeTalk("Bad\tName\n", true) == "TALK\tBad Name \t1\n");
    CHECK(encodeHello("0.1.0") == "HELLO\t1\t0.1.0\n");
    CHECK(encodeTsStatus(false) == "TS\t0\n");
}

TEST_CASE("normalizeName") {
    CHECK(normalizeName("  MiXeD Case\t") == "mixed case");
    CHECK(normalizeName("   ").empty());
}
