package io.ulalax.zlink.integration.bench;

import io.ulalax.zlink.Context;
import io.ulalax.zlink.ReceiveFlag;
import io.ulalax.zlink.SendFlag;
import io.ulalax.zlink.Socket;
import io.ulalax.zlink.SocketType;

import java.net.ServerSocket;

public final class PairBenchMain {
    public static void main(String[] args) {
        if (args.length < 3) {
            System.exit(1);
        }
        String pattern = args[0].toUpperCase();
        String transport = args[1];
        int size = Integer.parseInt(args[2]);
        if (!"PAIR".equals(pattern)) {
            System.exit(0);
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
