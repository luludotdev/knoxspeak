package dev.lulu.knoxspeak;

import zombie.characters.IsoPlayer;
import zombie.iso.IsoGridSquare;
import zombie.network.GameClient;
import zombie.scripting.objects.CharacterTrait;

import java.util.*;

public final class VoiceTick {

    private static final long FRAME_INTERVAL_MS = 50; // 20 Hz
    private static final float MIN_AUDIBLE_GAIN = 0.001f;

    private final Bridge bridge;
    private final StringBuilder frame = new StringBuilder(1024);
    private final LineOfSound lineOfSound = new LineOfSound();
    private VoiceMath.Settings settings = VoiceMath.Settings.DEFAULT;

    // radio must be this much louder than direct voice to take over
    static final float RADIO_TAKEOVER_FACTOR = 1.5f;
    static final float RADIO_KEEP_FACTOR = 1.2f;

    private final Radios radios = new Radios();
    private final Map<String, Heard> best = new HashMap<>();      // direct voice, then the chosen path
    private final Map<String, Heard> bestRadio = new HashMap<>(); // loudest radio path per speaker
    private final Set<String> onRadio = new HashSet<>();          // speakers heard over radio last frame

    private final Map<String, String> displayNames = new HashMap<>();
    private final Map<Short, String> namesById = new HashMap<>();
    private final Set<String> silencedNames = new HashSet<>();
    private final List<IsoPlayer> nearby = new ArrayList<>();

    private final Map<String, String> sentLines = new HashMap<>(); // kind -> last line sent

    private long lastFrameMs;
    private int seq;
    private boolean inSession; // HELLO has been sent for the current session
    private volatile boolean tsConnected;
    private volatile boolean micMuted;

    // listener options from lua mod
    private boolean radioFilter = true;
    private boolean radioStatic = true;
    private boolean hearingPenalties = true;
    private VoiceMath.Hearing traitHearing = VoiceMath.Hearing.NORMAL; // derived from traits
    private VoiceMath.Hearing hearing = VoiceMath.Hearing.NORMAL; // applied from above and if the setting is enabled

    private int sentGeneration = -1;
    private Listener listener;

    public VoiceTick(Bridge bridge) {
        this.bridge = bridge;
    }

    public boolean isTsConnected() {
        return tsConnected;
    }

    static boolean silenced(IsoPlayer p) {
        return p.isDead() || p.isInvisible();
    }

    //region user options
    public void setClientOptions(boolean radioFilter, boolean radioStatic, boolean hearingPenalties) {
        this.radioFilter = radioFilter;
        this.radioStatic = radioStatic;
        this.hearingPenalties = hearingPenalties;
        updateHearing();
    }

    private void updateHearing() {
        VoiceMath.Hearing h = hearingPenalties ? traitHearing : VoiceMath.Hearing.NORMAL;
        if (h != hearing) {
            hearing = h;
            Log.info("hearing traits: " + h + (hearingPenalties ? "" : " (penalties off)"));
        }
    }

    public boolean isMicMuted() {
        return micMuted;
    }

    public void setMicMuted(boolean muted) {
        micMuted = muted;
        bridge.send(Protocol.mute(muted));
    }

    public void setHearing(double range, double mufflePercent, double falloffStrength) {
        VoiceMath.Settings s = VoiceMath.Settings.DEFAULT.withHearing((float) range, (float) mufflePercent, (float) falloffStrength);
        if (!s.equals(settings)) {
            settings = s;
            Log.info("hearing: voice range " + s.maxRange() + " tiles, muffle " + Math.round(s.muffleScale() * 100) + "%, falloff exponent " + String.format(java.util.Locale.ROOT, "%.2f", s.falloffExponent()));
        }
    }
    //endregion

    //region signal params
    // calculate signal params for a given location
    private VoiceMath.Params fromSource(Listener me, float sx, float sy, float sz, IsoGridSquare sq, Object key, VoiceMath.Settings s, long now, boolean needsSight) {
        if (sq == null) return null;
        float dx = sx - me.x();
        float dy = sy - me.y();

        // range check
        if (dx * dx + dy * dy > s.maxRange() * s.maxRange()) {
            return null;
        }

        IsoGridSquare mySq = me.square();
        boolean meOutside = mySq.isOutside();
        boolean theyOutside = sq.isOutside();
        boolean sameBuilding = mySq.getBuilding() != null && mySq.getBuilding() == sq.getBuilding();
        boolean sameRoom = mySq.getRoom() != null && mySq.getRoom() == sq.getRoom();
        int myZ = (int) Math.floor(me.z());
        int floors = (int) Math.floor(sz) - myZ;
        // calculate sound path (rough approximation unfortunately) if on the same floor
        VoiceMath.Path path = floors == 0 && !sameRoom ? lineOfSound.between((int) Math.floor(me.x()), (int) Math.floor(me.y()), myZ, key, (int) Math.floor(sx), (int) Math.floor(sy), (int) Math.floor(sz), now) : VoiceMath.Path.CLEAR;
        boolean lineOfSight = floors == 0 ? path.seeThrough() : sq.isCouldSee(me.index());
        if (needsSight && !lineOfSight) {
            return null;
        }

        VoiceMath.Occlusion occ = VoiceMath.classify(meOutside, theyOutside, sameBuilding, sameRoom, lineOfSight);
        VoiceMath.Params p = VoiceMath.compute(dx, dy, floors, !(meOutside && theyOutside), occ, path, s);
        return nearField(p, dx, dy, floors, sameRoom, path, mySq, sq);
    }

    private static VoiceMath.Params nearField(VoiceMath.Params p, float dx, float dy, int floors, boolean sameRoom, VoiceMath.Path path, IsoGridSquare mySq, IsoGridSquare sq) {
        // check range to potentially avoid expensive trace
        if (p == null || p.muffle() <= 0f || Math.abs(floors) > 1 || !VoiceMath.inNearField(dx, dy)) {
            return p;
        }

        boolean open = floors == 0 ? VoiceMath.openOnSameFloor(sameRoom, path) : LineOfSound.openBetweenFloors(mySq, sq);
        return VoiceMath.nearField(p, dx, dy, open);
    }
    //endregion

    //region on-tick handlers
    private void drainInbox() {
        Protocol.PluginMessage msg;
        while ((msg = bridge.poll()) != null) {
            switch (msg) {
                case Protocol.Talk t -> setTalking(t.username(), t.talking());
                case Protocol.TsStatus s -> tsConnected = s.connected();
                case Protocol.Mic m -> micMuted = m.muted();
                case Protocol.PeerCarry c -> radios.setPeerCarry(c.username(), c.list());
                case Protocol.Hello h -> Log.info("TS plugin " + h.pluginVersion() + " (protocol " + h.version() + ")");
                case Protocol.Error e -> Log.warn("plugin: " + e.message());
            }
        }
    }

    // set talking indicator on a player
    private static void setTalking(String username, boolean talking) {
        String key = NameKeys.of(username);
        IsoPlayer me = IsoPlayer.getInstance();
        if (me != null && key.equals(NameKeys.of(me.getUsername()))) {
            me.isSpeek = talking && !silenced(me);
            return;
        }

        if (!GameClient.client) return;
        ArrayList<IsoPlayer> players = GameClient.instance.getPlayers();
        for (IsoPlayer p : players) {
            // linear search isnt ideal but w/e its probably fine
            if (p != null && key.equals(NameKeys.of(p.getUsername()))) {
                p.isSpeek = talking && !silenced(p);
                return;
            }
        }
    }

    public void tick() {
        drainInbox();

        if (!GameClient.client) {
            if (inSession) {
                // no longer in a server
                inSession = false;
                listener = null;
                sentLines.remove("silence");
                sentLines.remove("carry");
                bridge.leaveGame();
            }

            // singleplayer / main menu, bail early
            return;
        }

        VanillaVoice.suppress();
        IsoPlayer me = IsoPlayer.getInstance();
        boolean haveMe = me != null && me.getUsername() != null;
        if (haveMe) {
            bridge.setUsername(me.getUsername());
            inSession = true;
        }
        if (!inSession || !bridge.isConnected()) {
            return;
        }

        long now = System.currentTimeMillis();
        if (now - lastFrameMs < FRAME_INTERVAL_MS) return;
        lastFrameMs = now;

        if (haveMe) {
            IsoGridSquare sq = me.getCurrentSquare();
            if (sq != null) {
                // remember last location, needed for dead players (rip) to keep hearing alive players
                listener = new Listener(me, me.getOnlineID(), me.getX(), me.getY(), me.getZ(), sq, me.getIndex());
            }
            traitHearing = VoiceMath.Hearing.of(me.hasTrait(CharacterTrait.DEAF), me.hasTrait(CharacterTrait.HARD_OF_HEARING));
            updateHearing();
        }

        if (listener == null) return;
        syncPluginState(me, now);
        sendFrame(listener, now);
    }

    private void sendIfChanged(String kind, String line) {
        if (!line.equals(sentLines.put(kind, line))) {
            bridge.send(line);
        }
    }

    private void syncPluginState(IsoPlayer me, long now) {
        if (bridge.generation() != sentGeneration) {
            sentGeneration = bridge.generation();
            sentLines.clear();
        }

        sendIfChanged("radiofilter", Protocol.config("radiofilter", radioFilter));
        sendIfChanged("radiostatic", Protocol.config("radiostatic", radioStatic));
        sendIfChanged("silence", Protocol.silence(me != null && silenced(me)));
        // report our walkie to everyone so they can hear it
        Listener l = listener;
        sendIfChanged("carry", Protocol.carry(radios.refreshReceivers(l.self(), l.onlineId(), (x, y, z, sq, key, s) -> fromSource(l, x, y, z, sq, key, s, now, false))));
    }

    // offer direct/radio voice data as candidates for transmission
    private void offer(String username, Heard h) {
        if (h != null && h.radio()) {
            float rg = hearing.radioGain();
            if (rg != 1f) {
                h = new Heard(h.gain() * rg, h.muffle(), h.pan(), true, h.quality(), h.pickupMuffle());
            }
        }

        if (h == null || h.gain() <= MIN_AUDIBLE_GAIN) return;
        String key = NameKeys.of(username);
        Map<String, Heard> paths = h.radio() ? bestRadio : best;
        Heard cur = paths.get(key);
        if (cur == null || h.gain() > cur.gain()) {
            paths.put(key, h);
            displayNames.put(key, username);
        }
    }

    // pick between direct and radio, loudest wins
    // direct has a slight advantage
    private void mergeRadioPaths() {
        onRadio.retainAll(bestRadio.keySet());
        for (Map.Entry<String, Heard> e : bestRadio.entrySet()) {
            String key = e.getKey();
            Heard radio = e.getValue();
            Heard direct = best.get(key);
            float factor = onRadio.contains(key) ? RADIO_KEEP_FACTOR : RADIO_TAKEOVER_FACTOR;
            if (direct == null || radio.gain() > direct.gain() * factor) {
                best.put(key, radio);
                onRadio.add(key);
            } else {
                onRadio.remove(key);
            }
        }
    }

    private void sendFrame(Listener me, long now) {
        best.clear();
        bestRadio.clear();
        displayNames.clear();
        silencedNames.clear();
        nearby.clear();

        // direct voice from loaded players
        VoiceMath.Settings direct = hearing.limit(settings);
        ArrayList<IsoPlayer> players = GameClient.instance.getPlayers();
        for (IsoPlayer p : players) {
            if (p == null || p == me.self() || p.isLocalPlayer() || p.getUsername() == null) {
                continue;
            }
            // save for radio path later
            namesById.put(p.getOnlineID(), p.getUsername());
            if (silenced(p)) {
                silencedNames.add(NameKeys.of(p.getUsername()));
                continue;
            }
            IsoGridSquare sq = p.getCurrentSquare();
            if (sq == null) {
                // not loaded, cannot be heard directly
                continue;
            }
            nearby.add(p);
            VoiceMath.Params v = hearing.apply(fromSource(me, p.getX(), p.getY(), p.getZ(), sq, p, direct, now, hearing.needsSight()));
            if (v != null) offer(p.getUsername(), Heard.direct(v.gain(), v.muffle(), v.pan()));
        }

        // voice heard over radios
        radios.collect(me.onlineId(), namesById, silencedNames, nearby, me.x(), me.y(), settings, (x, y, z, sq, key, s) -> fromSource(me, x, y, z, sq, key, s, now, false), this::offer);
        mergeRadioPaths();

        // assemble entire frame data
        int frameSeq = seq++;
        frame.setLength(0);
        Protocol.appendFrameBegin(frame, frameSeq);
        for (Map.Entry<String, Heard> e : best.entrySet()) {
            Heard h = e.getValue();
            String name = displayNames.get(e.getKey());
            if (h.radio()) {
                Protocol.appendRadioSpeaker(frame, name, h.gain(), h.muffle(), h.pan(), h.quality(), h.pickupMuffle());
            } else {
                Protocol.appendSpeaker(frame, name, h.gain(), h.muffle(), h.pan());
            }
        }
        Protocol.appendFrameEnd(frame, frameSeq);
        bridge.send(frame.toString());
    }
    //endregion

    //region data records
    // audio effects for a given speaker
    record Heard(float gain, float muffle, float pan, boolean radio, float quality, float pickupMuffle) {
        static Heard direct(float gain, float muffle, float pan) {
            return new Heard(gain, muffle, pan, false, 1f, 0f);
        }
    }

    // local player info
    private record Listener(IsoPlayer self, short onlineId, float x, float y, float z, IsoGridSquare square, int index) {
    }
    //endregion
}
