package dev.lulu.knoxspeak;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.Objects;
import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.atomic.AtomicInteger;

public final class Bridge {
    private static final long RECONNECT_DELAY_MS = 2000;
    private static final int CONNECT_TIMEOUT_MS = 500;

    private final int port;
    private final String modVersion;
    private final Queue<Protocol.PluginMessage> inbox = new ConcurrentLinkedQueue<>();
    private final Object writeLock = new Object();

    private volatile Socket socket;
    private volatile OutputStream out;
    private volatile String username;
    private volatile boolean running;
    private Thread thread;
    private final AtomicInteger generation = new AtomicInteger();
    private volatile boolean serverLacksMod;

    public Bridge(int port, String modVersion) {
        this.port = port;
        this.modVersion = modVersion;
    }

    //region lifecycle
    public synchronized void start() {
        if (running) return;
        running = true;

        thread = new Thread(this::run, "knoxspeak-bridge");
        thread.setDaemon(true);
        thread.start();
    }

    public synchronized void stop() {
        running = false;
        send(Protocol.bye());
        closeQuietly();
        if (thread != null) thread.interrupt();
    }

    private void run() {
        while (running) {
            try (final Socket s = new Socket()) {
                s.setTcpNoDelay(true);
                s.connect(new InetSocketAddress(InetAddress.getLoopbackAddress(), port), CONNECT_TIMEOUT_MS);
                socket = s;
                out = s.getOutputStream();
                generation.incrementAndGet();
                Log.info("connected to TS plugin on port " + port);

                final String name = username;
                if (name != null) send(Protocol.hello(modVersion, name));
                else if (serverLacksMod) send(Protocol.noMod(true));

                BufferedReader in = new BufferedReader(new InputStreamReader(s.getInputStream(), StandardCharsets.UTF_8));
                String line;
                while (running && (line = in.readLine()) != null) {
                    final Protocol.PluginMessage msg = Protocol.parse(line);
                    if (msg != null) inbox.add(msg);
                }
            } catch (IOException e) {
                // plugin not running yet / TS closed: retry quietly
            } finally {
                boolean wasConnected = out != null;
                out = null;
                socket = null;
                if (wasConnected) {
                    Log.info("disconnected from TS plugin");
                }
            }

            if (running) {
                try {
                    Thread.sleep(RECONNECT_DELAY_MS);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    return;
                }
            }
        }
    }

    private void closeQuietly() {
        Socket s = socket;
        out = null;
        if (s != null) {
            try {
                s.close();
            } catch (IOException ignored) {
                // nothing to do
            }
        }
    }
    //endregion

    //region message i/o
    public Protocol.PluginMessage poll() {
        return inbox.poll();
    }

    public void send(String lines) {
        OutputStream o = out;
        if (o == null) {
            return;
        }
        synchronized (writeLock) {
            try {
                o.write(lines.getBytes(StandardCharsets.UTF_8));
                o.flush();
            } catch (IOException e) {
                closeQuietly(); // reader thread notices and reconnects
            }
        }
    }
    //endregion

    //region state getters
    public boolean isConnected() {
        return out != null;
    }

    public int generation() {
        return generation.get();
    }
    //endregion

    //region message senders
    public void setUsername(String name) {
        if (!Objects.equals(name, username)) {
            username = name;
            if (name != null) {
                send(Protocol.hello(modVersion, name));
            }
        }
    }

    public void setServerLacksMod(boolean lacking) {
        if (lacking != serverLacksMod) {
            serverLacksMod = lacking;
            send(Protocol.noMod(lacking));
        }
    }

    public void leaveGame() {
        username = null;
        send(Protocol.bye());
    }
    //endregion
}
