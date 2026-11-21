// automatically generated from projectzomboid.jar (sha256 3c4a7c5f149d), ZombieBuddy.jar (sha256 eb79b9876332).
// do not edit
package zombie.core.raknet;

public class VoiceManagerData {
    public static java.util.ArrayList<zombie.core.raknet.VoiceManagerData> data;
    public short index;
    public java.util.ArrayList<zombie.core.raknet.VoiceManagerData.RadioData> radioData;
    public static zombie.core.raknet.VoiceManagerData get(short p0) { throw new UnsupportedOperationException("stub"); }
    public static class RadioData {
        public float distance;
        public int freq;
        public short x;
        public short y;
        public zombie.radio.devices.DeviceData getDeviceData() { throw new UnsupportedOperationException("stub"); }
        public boolean isReceivingAvailable(int p0) { throw new UnsupportedOperationException("stub"); }
    }
}
