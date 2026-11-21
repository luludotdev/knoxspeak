package dev.lulu.knoxspeak;

import zombie.characters.IsoPlayer;
import zombie.core.raknet.VoiceManagerData;
import zombie.inventory.types.Radio;
import zombie.iso.IsoGridSquare;
import zombie.iso.objects.IsoWaveSignal;
import zombie.radio.devices.DeviceData;
import zombie.vehicles.BaseVehicle;
import zombie.vehicles.VehiclePart;

import java.util.*;
import java.util.function.BiConsumer;
import java.util.function.Consumer;

final class Radios {
    static final float LOUDSPEAKER_RANGE = 12f;   // tiles a placed/vehicle radio carries at full volume
    static final float OTHERS_WALKIE_RANGE = 4f;  // tiles others hear a walkie without headphones
    static final float OTHERS_WALKIE_GAIN = 0.35f;
    static final float VEHICLE_BODY_MUFFLE = 0.3f; // hearing a car radio from outside the car

    static final float PICKUP_RANGE = 4f; // placed radio mic range
    private static final VoiceMath.Settings PICKUP = VoiceMath.Settings.DEFAULT.withRange(PICKUP_RANGE);

    // maps for radio pickup, synced via the teamspeak plugin
    final Map<String, List<Carried>> peerCarry = new HashMap<>();
    private final Map<String, Map<Integer, Pickup>> peerPickup = new HashMap<>();

    private final List<Receiver> receivers = new ArrayList<>();
    private final TreeSet<String> carry = new TreeSet<>();
    private final Map<Integer, Pickup> ownBest = new TreeMap<>();
    private final StringBuilder scratch = new StringBuilder(64);
    private final List<VoiceManagerData> all = new ArrayList<>();
    private final List<Tx> tx = new ArrayList<>();

    private VoiceMath.Settings rangeBase;
    private VoiceMath.Settings loudspeaker;
    private VoiceMath.Settings othersWalkie;

    enum Kind {
        CARRIED,
        MY_VEHICLE,
        LOUDSPEAKER
    }

    interface Proximity {
        VoiceMath.Params from(float x, float y, float z, IsoGridSquare sq, Object key, VoiceMath.Settings s);
    }

    void setPeerCarry(String username, String payload) {
        String key = NameKeys.of(username);
        int bar = payload.indexOf('|');
        List<Carried> walkies = new ArrayList<>();
        eachTriple(bar < 0 ? payload : payload.substring(0, bar), f -> walkies.add(new Carried(Integer.parseInt(f[0]), "1".equals(f[1]), Float.parseFloat(f[2]))));
        Map<Integer, Pickup> pickup = new HashMap<>();
        if (bar >= 0) {
            eachTriple(payload.substring(bar + 1), f -> pickup.put(Integer.parseInt(f[0]), new Pickup(VoiceMath.clamp01(Float.parseFloat(f[1])), VoiceMath.clamp01(Float.parseFloat(f[2])))));
        }
        putOrRemove(peerPickup, key, pickup.isEmpty() ? null : pickup);
        putOrRemove(peerCarry, key, walkies.isEmpty() ? null : walkies);
    }

    //region receivers
    String refreshReceivers(IsoPlayer self, short myId, Proximity prox) {
        receivers.clear();
        if (myId < 0) return "";

        String walkies = collectReceivers(self, myId);
        String pickup = ownPickup(prox);
        return pickup.isEmpty() ? walkies : walkies + "|" + pickup;
    }

    private String collectReceivers(IsoPlayer self, short myId) {
        carry.clear();
        VoiceManagerData mine = VoiceManagerData.get(myId);
        synchronized (mine.radioData) {
            for (int i = 1; i < mine.radioData.size(); i++) {
                VoiceManagerData.RadioData r = mine.radioData.get(i);
                DeviceData dd = r.getDeviceData();
                if (dd == null) continue;

                Receiver rx = classify(r, dd, self);
                if (rx == null) continue;

                receivers.add(rx);
                if (rx.kind() == Kind.CARRIED && dd.getIsTurnedOn() && !dd.isPlayingMedia()) {
                    StringBuilder sb = scratch;
                    sb.setLength(0);
                    sb.append(dd.getChannel()).append(':').append(dd.getHeadphoneType() >= 0 ? 1 : 0).append(':');
                    carry.add(Protocol.appendFixed(sb, dd.getDeviceVolume(), 2).toString());
                }
            }
        }
        return carry.isEmpty() ? "" : String.join(";", carry);
    }

    private static Receiver classify(VoiceManagerData.RadioData r, DeviceData dd, IsoPlayer self) {
        Object parent = dd.getParent();
        if (parent instanceof Radio item) {
            if (item.getWorldItem() != null) {
                return loudspeakerAt(r, dd, item.getWorldItem().getSquare());
            }
            if (self != null && item.getContainer() == self.getInventory()) {
                return new Receiver(r, dd, Kind.CARRIED, self.getX(), self.getY(), self.getZ(), self.getCurrentSquare(), false);
            }
            return null; // vanilla only lists top-level inventory and world items
        }

        if (parent instanceof IsoWaveSignal placed) {
            return loudspeakerAt(r, dd, placed.getSquare());
        }

        if (parent instanceof VehiclePart part) {
            BaseVehicle v = part.getVehicle();
            if (v == null) return null;

            if (self != null && self.getVehicle() == v) {
                return new Receiver(r, dd, Kind.MY_VEHICLE, v.getX(), v.getY(), v.getZ(), v.getCurrentSquare(), true);
            }
            IsoGridSquare sq = v.getCurrentSquare();
            return sq == null ? null : new Receiver(r, dd, Kind.LOUDSPEAKER, v.getX(), v.getY(), v.getZ(), sq, true);
        }

        return null;
    }

    private static Receiver loudspeakerAt(VoiceManagerData.RadioData r, DeviceData dd, IsoGridSquare sq) {
        return sq == null ? null : new Receiver(r, dd, Kind.LOUDSPEAKER, sq.getX() + 0.5f, sq.getY() + 0.5f, sq.getZ(), sq, false);
    }
    //endregion

    //region mic pickup
    // calculate and record our own radio pickup
    private String ownPickup(Proximity prox) {
        Map<Integer, Pickup> best = ownBest;
        best.clear();
        for (Receiver rx : receivers) {
            int freq = rx.data().freq;
            if (freq == 0 || rx.data().distance <= 0) continue;

            Pickup p;
            if (rx.kind() == Kind.LOUDSPEAKER) {
                VoiceMath.Params v = prox.from(rx.x(), rx.y(), rx.z(), rx.square(), rx.device(), PICKUP);
                p = v == null ? new Pickup(0f, 1f) : new Pickup(v.gain(), v.muffle());
            } else {
                p = Pickup.FULL;
            }
            Pickup cur = best.get(freq);
            if (cur == null || p.gain() > cur.gain() || (p.gain() == cur.gain() && p.muffle() < cur.muffle())) {
                best.put(freq, p);
            }
        }
        if (best.isEmpty()) return "";

        StringBuilder sb = scratch;
        sb.setLength(0);
        for (Map.Entry<Integer, Pickup> e : best.entrySet()) {
            float g = VoiceMath.round1(e.getValue().gain());
            float m = VoiceMath.round1(e.getValue().muffle());
            if (g >= 1f && m <= 0f) {
                continue; // perfect: the default
            }
            if (!sb.isEmpty()) {
                sb.append(';');
            }
            Protocol.appendFixed(sb.append(e.getKey()).append(':'), g, 1).append(':');
            Protocol.appendFixed(sb, m, 1);
        }
        return sb.isEmpty() ? "" : sb.toString();
    }

    record Pickup(float gain, float muffle) {
        // walkie in hand or sitting in a car with a ham
        static final Pickup FULL = new Pickup(1f, 0f);
    }

    private Pickup pickupFor(String speaker, int freq) {
        Map<Integer, Pickup> m = peerPickup.get(NameKeys.of(speaker));
        Pickup p = m == null ? null : m.get(freq);
        return p == null ? Pickup.FULL : p;
    }
    //endregion

    //region transmission
    private record Tx(int freq, float range, float x, float y) {
    }

    record Carried(int freq, boolean headphones, float volume) {
    }

    record Receiver(VoiceManagerData.RadioData data, DeviceData device, Kind kind, float x, float y, float z,
                    IsoGridSquare square, boolean inVehicle) {
    }

    // collect radio transmission candidates
    void collect(short myId, Map<Short, String> names, java.util.Set<String> silenced, List<IsoPlayer> nearby, float listenerX, float listenerY, VoiceMath.Settings base, Proximity prox, BiConsumer<String, VoiceTick.Heard> out) {
        all.clear();
        try {
            List<VoiceManagerData> src = VoiceManagerData.data;
            all.addAll(src);
        } catch (RuntimeException e) {
            // try again next frame
            return;
        }
        if (base != rangeBase) {
            rangeBase = base;
            loudspeaker = base.withRange(LOUDSPEAKER_RANGE);
            othersWalkie = base.withRange(OTHERS_WALKIE_RANGE);
        }

        for (VoiceManagerData d : all) {
            if (d == null || d.index == myId) continue;

            String name = names.get(d.index);
            if (name == null || silenced.contains(NameKeys.of(name))) {
                continue;
            }

            tx.clear();
            synchronized (d.radioData) {
                for (int j = 1; j < d.radioData.size(); j++) {
                    VoiceManagerData.RadioData t = d.radioData.get(j);
                    if (t.freq != 0 && t.distance > 0) {
                        tx.add(new Tx(t.freq, t.distance, t.x, t.y));
                    }
                }
            }

            for (Tx t : tx) {
                // how their radio mic hears them, muffling etc
                Pickup pick = pickupFor(name, t.freq());
                if (pick.gain() <= 0f) {
                    // too far from (or walled off from) the radio that would transmit them
                    continue;
                }

                for (Receiver rx : receivers) {
                    // vanilla radio (frequency, volume, etc) check
                    if (!rx.data().isReceivingAvailable(t.freq())) continue;

                    float dist = dist(rx.data().x, rx.data().y, t.x(), t.y());
                    if (dist >= t.range()) continue;

                    float quality = 1f - dist / t.range();
                    float vol = rx.device().getDeviceVolume();
                    switch (rx.kind()) {
                        case CARRIED, MY_VEHICLE ->
                            out.accept(name, new VoiceTick.Heard(vol * pick.gain(), 0f, 0f, true, quality, pick.muffle()));
                        case LOUDSPEAKER -> {
                            VoiceMath.Params p = prox.from(rx.x(), rx.y(), rx.z(), rx.square(), rx.device(), loudspeaker);
                            if (p != null) {
                                float m = rx.inVehicle() ? p.muffle() + VEHICLE_BODY_MUFFLE * (1f - p.muffle()) : p.muffle();
                                out.accept(name, new VoiceTick.Heard(p.gain() * vol * pick.gain(), m, p.pan(), true, quality, pick.muffle()));
                            }
                        }
                    }
                }

                // Other people's walkies, heard from next to them (never with headphones in).
                for (IsoPlayer o : nearby) {
                    List<Carried> theirs = peerCarry.get(NameKeys.of(o.getUsername()));
                    if (theirs == null || name.equals(o.getUsername())) continue;
                    if (dist(o.getX(), o.getY(), listenerX, listenerY) > OTHERS_WALKIE_RANGE) continue;

                    for (Carried w : theirs) {
                        // ignore if radio is on the wrong frequency, muted, or the user is wearing headphones
                        if (w.headphones() || w.volume() <= 0 || w.freq() != t.freq()) {
                            continue;
                        }

                        float dist = dist(o.getX(), o.getY(), t.x(), t.y());
                        if (dist >= t.range()) continue;

                        VoiceMath.Params p = prox.from(o.getX(), o.getY(), o.getZ(), o.getCurrentSquare(), o, othersWalkie);
                        if (p != null) {
                            out.accept(name, new VoiceTick.Heard(p.gain() * w.volume() * OTHERS_WALKIE_GAIN * pick.gain(), p.muffle(), p.pan(), true, 1f - dist / t.range(), pick.muffle()));
                        }
                    }
                }
            }
        }
    }
    //endregion

    //region utils
    // parse an `a;b;c` triple
    private static void eachTriple(String list, Consumer<String[]> parse) {
        for (String item : list.split(";")) {
            String[] f = item.split(":");
            if (f.length == 3) {
                try {
                    parse.accept(f);
                } catch (NumberFormatException ignored) {
                    // skip malformed entries
                }
            }
        }
    }

    private static <K, V> void putOrRemove(Map<K, V> map, K key, V value) {
        if (value == null) {
            map.remove(key);
        } else {
            map.put(key, value);
        }
    }

    private static float dist(float ax, float ay, float bx, float by) {
        float dx = ax - bx, dy = ay - by;
        return (float) Math.sqrt(dx * dx + dy * dy);
    }
    //endregion
}
