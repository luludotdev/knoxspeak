package dev.lulu.knoxspeak;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;

class ProtocolTest {
    @Test
    void encodesFrameLines() {
        assertEquals("HELLO\t1\t0.1.0\tLulu\n", Protocol.hello("0.1.0", "Lulu"));
        assertEquals("FRAME\t4294967295\n", Protocol.appendFrameBegin(new StringBuilder(), -1).toString()); // unsigned wrap
        assertEquals("SPK\tBob\t1.000\t0.250\t-1.000\n",
            Protocol.appendSpeaker(new StringBuilder(), "Bob", 2f, 0.25f, -5f).toString());
        assertEquals("END\t7\n", Protocol.appendFrameEnd(new StringBuilder(), 7).toString());
    }

    @Test
    void encodesRadioSpeaker() {
        // radio flag 1, then quality and pickupMuffle clamped to 0..1
        assertEquals("SPK\tCarol\t0.500\t0.000\t0.250\t1\t1.000\t0.000\n",
            Protocol.appendRadioSpeaker(new StringBuilder(), "Carol", 0.5f, 0f, 0.25f, 1.5f, -0.2f).toString());
        assertEquals("SPK\tCarol\t1.000\t0.300\t-0.500\t1\t0.042\t0.750\n",
            Protocol.appendRadioSpeaker(new StringBuilder(), "Carol", 1f, 0.3f, -0.5f, 0.042f, 0.75f).toString());
    }

    @Test
    void sanitizesFields() {
        assertEquals("SPK\tBad Name\t0.000\t0.000\t0.000\n",
            Protocol.appendSpeaker(new StringBuilder(), "Bad\tName", Float.NaN, 0, 0).toString());
    }

    @Test
    void parsesPluginMessages() {
        assertEquals(new Protocol.Talk("Bob", true), Protocol.parse("TALK\tBob\t1"));
        assertEquals(new Protocol.TsStatus(false), Protocol.parse("TS\t0\r"));
        assertEquals(new Protocol.Hello(1, "0.1.0"), Protocol.parse("HELLO\t1\t0.1.0"));
        assertNull(Protocol.parse("HELLO\tx\t0.1.0"));
        assertNull(Protocol.parse("WAT"));
        assertNull(Protocol.parse("TALK\t\t1"));
    }

    @Test
    void micMute() {
        assertEquals("MUTE\t1\n", Protocol.mute(true));
        assertEquals("MUTE\t0\n", Protocol.mute(false));
        assertEquals(new Protocol.Mic(true), Protocol.parse("MIC\t1"));
        assertEquals(new Protocol.Mic(false), Protocol.parse("MIC\t0"));
        assertEquals("BYE\n", Protocol.bye());
    }

    @Test
    void normalizesNames() {
        assertEquals("mixed case", Protocol.normalizeName("  MiXeD Case\t"));
        assertEquals("Äb", Protocol.normalizeName("ÄB")); // ascii-only, matches zomboid (why does it do it this way???)
    }
}
