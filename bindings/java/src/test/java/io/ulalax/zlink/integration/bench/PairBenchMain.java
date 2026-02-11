package io.ulalax.zlink.integration.bench;

import io.ulalax.zlink.Context;
import io.ulalax.zlink.ReceiveFlag;
import io.ulalax.zlink.SendFlag;
import io.ulalax.zlink.Socket;
import io.ulalax.zlink.SocketType;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.ServerSocket;
import java.util.HashMap;
import java.util.Map;

public final class PairBenchMain {
    private static final Map<String, String> CORE_BIN = new HashMap<>();

    static {
        CORE_BIN.put("PUBSUB", "comp_current_pubsub");
        CORE_BIN.put("DEALER_DEALER", "comp_current_dealer_dealer");
        CORE_BIN.put("DEALER_ROUTER", "comp_current_dealer_router");
        CORE_BIN.put("ROUTER_ROUTER", "comp_current_router_router");
        CORE_BIN.put("ROUTER_ROUTER_POLL", "comp_current_router_router_poll");
        CORE_BIN.put("STREAM", "comp_current_stream");
        CORE_BIN.put("GATEWAY", "comp_current_gateway");
        CORE_BIN.put("SPOT", "comp_current_spot");
    }

    public static void main(String[] args) {
        if (args.length < 3) {
            System.exit(1);
        }
        String pattern = args[0].toUpperCase();
        String transport = args[1];
        int size = Integer.parseInt(args[2]);
        String coreDir = args.length >= 4 ? args[3] : "";

        if (!"PAIR".equals(pattern)) {
            System.exit(runCore(pattern, transport, args[2], coreDir));
        }

        int warmup = parseEnv("BENCH_WARMUP_COUNT", 1000);
        int latCount = parseEnv("BENCH_LAT_COUNT", 500);
        int msgCount = resolveMsgCount(size);

        Context ctx = new Context();
        Socket a = new Socket(ctx, SocketType.PAIR);
        Socket b = new Socket(ctx, SocketType.PAIR);

        try {
            String endpoint = endpointFor(transport);
            a.bind(endpoint);
            b.connect(endpoint);
            Thread.sleep(50);

            byte[] buf = new byte[size];
            for (int i = 0; i < size; i++) buf[i] = 'a';

            for (int i = 0; i < warmup; i++) {
                b.send(buf, SendFlag.NONE);
                a.recv(size, ReceiveFlag.NONE);
            }

            long t0 = System.nanoTime();
            for (int i = 0; i < latCount; i++) {
                b.send(buf, SendFlag.NONE);
                byte[] x = a.recv(size, ReceiveFlag.NONE);
                a.send(x, SendFlag.NONE);
                b.recv(size, ReceiveFlag.NONE);
            }
            double latUs = (System.nanoTime() - t0) / 1000.0 / (latCount * 2.0);

            t0 = System.nanoTime();
            for (int i = 0; i < msgCount; i++) {
                b.send(buf, SendFlag.NONE);
            }
            for (int i = 0; i < msgCount; i++) {
                a.recv(size, ReceiveFlag.NONE);
            }
            double sec = (System.nanoTime() - t0) / 1_000_000_000.0;
            double thr = msgCount / sec;

            System.out.println("RESULT,current,PAIR," + transport + "," + size + ",throughput," + thr);
            System.out.println("RESULT,current,PAIR," + transport + "," + size + ",latency," + latUs);
            System.exit(0);
        } catch (Exception e) {
            System.exit(2);
        } finally {
            try { a.close(); } catch (Exception ignored) {}
            try { b.close(); } catch (Exception ignored) {}
            try { ctx.close(); } catch (Exception ignored) {}
        }
    }

    private static int runCore(String pattern, String transport, String sizeArg, String coreDirArg) {
        String bin = CORE_BIN.get(pattern);
        if (bin == null) {
            return 0;
        }

        String coreDir = coreDirArg;
        if (coreDir == null || coreDir.isEmpty()) {
            coreDir = System.getenv("ZLINK_CORE_BENCH_DIR");
            if (coreDir == null) coreDir = "";
        }
        if (coreDir.isEmpty()) {
            System.err.println("core bench dir is required for pattern " + pattern);
            return 2;
        }

        try {
            ProcessBuilder pb = new ProcessBuilder(coreDir + "/" + bin, "current", transport, sizeArg);
            Process p = pb.start();

            try (BufferedReader br = new BufferedReader(new InputStreamReader(p.getInputStream()))) {
                String line;
                while ((line = br.readLine()) != null) {
                    System.out.println(line);
                }
            }
            try (BufferedReader br = new BufferedReader(new InputStreamReader(p.getErrorStream()))) {
                String line;
                while ((line = br.readLine()) != null) {
                    System.err.println(line);
                }
            }

            p.waitFor();
            return p.exitValue();
        } catch (Exception e) {
            return 2;
        }
    }

    private static int parseEnv(String name, int def) {
        String v = System.getenv(name);
        if (v == null || v.isEmpty()) return def;
        try {
            int p = Integer.parseInt(v);
            return p > 0 ? p : def;
        } catch (Exception ignored) {
            return def;
        }
    }

    private static int resolveMsgCount(int size) {
        String v = System.getenv("BENCH_MSG_COUNT");
        if (v != null && !v.isEmpty()) {
            try {
                int p = Integer.parseInt(v);
                if (p > 0) return p;
            } catch (Exception ignored) {
            }
        }
        return size <= 1024 ? 200000 : 20000;
    }

    private static String endpointFor(String transport) {
        if ("inproc".equals(transport)) {
            return "inproc://bench-pair-" + System.currentTimeMillis();
        }
        return transport + "://127.0.0.1:" + getPort();
    }

    private static int getPort() {
        try (ServerSocket socket = new ServerSocket(0)) {
            return socket.getLocalPort();
        } catch (Exception ex) {
            throw new RuntimeException(ex);
        }
    }
}
