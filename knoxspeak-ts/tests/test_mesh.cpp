#include "core/mesh.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace knoxspeak::mesh;

TEST_CASE("mesh round-trips") {
    auto a = parse(encodeAnnounce("J4N3"));
    REQUIRE(a);
    CHECK(std::get<Announce>(*a).username == "J4N3");
    CHECK(std::holds_alternative<Query>(*parse(encodeQuery())));
    CHECK(std::holds_alternative<Idle>(*parse(encodeIdle())));
    CHECK(std::holds_alternative<Leave>(*parse(encodeLeave())));
}

TEST_CASE("mesh wire format") {
    CHECK(encodeAnnounce("Mario") == "KNOXSPEAK\t1\tANNOUNCE\tMario");
    CHECK(encodeQuery() == "KNOXSPEAK\t1\tQUERY");
}

TEST_CASE("mesh accepts two-letter PZ names that TS nicknames can't hold") {
    auto a = parse("KNOXSPEAK\t1\tANNOUNCE\tAl");
    REQUIRE(a);
    CHECK(std::get<Announce>(*a).username == "Al");
}

TEST_CASE("mesh ignores foreign or invalid commands") {
    CHECK_FALSE(parse(""));
    CHECK_FALSE(parse("TFAR\t1\tANNOUNCE\tBuilder"));
    CHECK_FALSE(parse("KNOXSPEAK\t2\tANNOUNCE\tBuilder")); // other protocol version
    CHECK_FALSE(parse("KNOXSPEAK\tx\tQUERY"));
    CHECK_FALSE(parse("KNOXSPEAK\t1"));
    CHECK_FALSE(parse("KNOXSPEAK\t1\tANNOUNCE"));
    CHECK_FALSE(parse("KNOXSPEAK\t1\tANNOUNCE\tB"));                       // too short
    CHECK_FALSE(parse("KNOXSPEAK\t1\tANNOUNCE\t" + std::string(33, 'a'))); // too long
    CHECK_FALSE(parse("KNOXSPEAK\t1\tWAT"));
}

TEST_CASE("validUsername counts characters like Java, not bytes") {
    std::string cyr32;
    for (int i = 0; i < 32; ++i) {
        cyr32 += "\xD0\x96"; // 'Ж': 2 bytes, 1 UTF-16 unit
    }
    CHECK(cyr32.size() == 64);
    CHECK(validUsername(cyr32));                                           // 32 chars: allowed, though 64 bytes
    CHECK_FALSE(validUsername(cyr32 + "\xD0\x96"));                        // 33 chars
    CHECK(validUsername("J\xC3\xBCrgen") == "J\xC3\xBCrgen");              // "Jürgen"
    CHECK(validUsername("\xF0\x9F\x98\x80"));                              // one emoji = 2 units: meets the minimum
    CHECK_FALSE(validUsername(std::string(31, 'a') + "\xF0\x9F\x98\x80")); // 31 + 2 = 33 units
    CHECK_FALSE(validUsername("\xE4\xB8\xAD"));                            // a single CJK char = 1 unit: too short
}

TEST_CASE("validUsername trims and bounds") {
    CHECK(validUsername("  Happy ") == "Happy");
    CHECK_FALSE(validUsername("   "));
    CHECK_FALSE(validUsername("a"));
    CHECK(validUsername(std::string(32, 'a')));
    CHECK_FALSE(validUsername(std::string(33, 'a')));
}
