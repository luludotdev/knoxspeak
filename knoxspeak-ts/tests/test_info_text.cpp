#include "core/info_text.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace knoxspeak::info;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("self status lines") {
    CHECK_THAT(selfText({.gameConnected = false, .version = "0.1.0"}), ContainsSubstring("not connected"));
    CHECK_THAT(selfText({.gameConnected = true, .version = "0.1.0"}), ContainsSubstring("not in a multiplayer game"));
    CHECK_THAT(selfText({.gameConnected = true, .username = "Radio Seesaw", .voiceLive = true, .version = "0.1.0"}),
               ContainsSubstring("In game as [b]Radio Seesaw[/b]: positional voice active"));
    CHECK_THAT(selfText({.gameConnected = true, .username = "Radio Seesaw", .voiceLive = false, .version = "0.1.0"}),
               ContainsSubstring("everyone muted")); // strict mode
    CHECK_THAT(selfText({.version = "0.1.0+gabc"}), ContainsSubstring("knoxspeak 0.1.0+gabc"));
}

TEST_CASE("peer status lines") {
    CHECK_THAT(peerText({}), ContainsSubstring("not detected"));
    CHECK_THAT(peerText({.kind = PeerKind::Idle}), ContainsSubstring("not in game"));

    CHECK_THAT(peerText({.kind = PeerKind::Playing, .username = "Doc Armstrong"}), ContainsSubstring("In game as [b]Doc Armstrong[/b]"));
}

TEST_CASE("usernames can't inject BBCode") {
    PeerState p{.kind = PeerKind::Playing, .username = "[url=x]Scarlet[/url]"};
    CHECK_FALSE(peerText(p).contains("[url"));
}

TEST_CASE("server summary") {
    CHECK(serverText(3, 5) == "3 in game, 5 with knoxspeak");
}
