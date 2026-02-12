package dev.kairoscode.zlink.integration.bench;

import dev.kairoscode.zlink.*;

import java.lang.foreign.MemorySegment;
import java.net.ServerSocket;
import java.util.concurrent.locks.LockSupport;

final class BenchUtil {
    private BenchUtil() {
    }

    static boolean waitForInput(Socket socket, int timeoutMs) {
        Poller poller = new Poller();
        poller.add(socket, PollEventType.POLLIN);
        return !poller.poll(timeoutMs).isEmpty();
    }

    static byte[] streamExpectConnectEvent(Socket socket) {
        for (int i = 0; i < 64; i++) {
            byte[] rid = socket.recv(256, ReceiveFlag.NONE);
            byte[] payload = socket.recv(16, ReceiveFlag.NONE);
            if (payload.length == 1 && payload[0] == 0x01) {
                return rid;
            }
        }
        throw new RuntimeException("invalid STREAM connect event");
    }

    static void streamSend(Socket socket, byte[] rid, byte[] payload) {
        socket.send(rid, SendFlag.SNDMORE);
        socket.send(payload, SendFlag.NONE);
    }

    static void streamSend(Socket socket,
                           MemorySegment rid,
                           int ridLen,
                           MemorySegment payload,
                           int payloadLen) {
        socket.send(rid, 0, ridLen, SendFlag.SNDMORE);
        socket.send(payload, 0, payloadLen, SendFlag.NONE);
    }

    static void streamSendConst(Socket socket,
                                MemorySegment rid,
                                int ridLen,
                                MemorySegment payload,
                                int payloadLen) {
        socket.sendConst(rid, 0, ridLen, SendFlag.SNDMORE);
        socket.sendConst(payload, 0, payloadLen, SendFlag.NONE);
    }

    static StreamFrame streamRecv(Socket socket, int cap) {
        byte[] rid = socket.recv(256, ReceiveFlag.NONE);
        byte[] payload = socket.recv(cap, ReceiveFlag.NONE);
        return new StreamFrame(rid, payload);
    }

    static int streamRecv(Socket socket,
                          MemorySegment rid,
                          int ridCap,
                          MemorySegment payload,
                          int payloadCap) {
        socket.recv(rid, 0, ridCap, ReceiveFlag.NONE);
        return socket.recv(payload, 0, payloadCap, ReceiveFlag.NONE);
    }

    static void printResult(String pattern, String transport, int size, double thr, double latUs) {
        System.out.println("RESULT,current," + pattern + "," + transport + ","
          + size + ",throughput," + thr);
        System.out.println("RESULT,current," + pattern + "," + transport + ","
          + size + ",latency," + latUs);
    }

    static boolean waitUntil(Check check, int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        while (System.currentTimeMillis() < deadline) {
            try {
                if (check.get()) {
                    return true;
                }
            } catch (Exception ignored) {
            }
            sleep(10);
        }
        return false;
    }

    static byte[] recvWithTimeout(Socket socket, int size, int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        while (System.currentTimeMillis() < deadline) {
            try {
                return socket.recv(size, ReceiveFlag.DONTWAIT);
            } catch (Exception ignored) {
                LockSupport.parkNanos(100_000L);
            }
        }
        throw new RuntimeException("timeout");
    }

    static int recvWithTimeout(Socket socket, MemorySegment dst, int size,
                               int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        while (System.currentTimeMillis() < deadline) {
            try {
                return socket.recv(dst, 0, size, ReceiveFlag.DONTWAIT);
            } catch (Exception ignored) {
                LockSupport.parkNanos(100_000L);
            }
        }
        throw new RuntimeException("timeout");
    }

    static byte[] recvBlocking(Socket socket, int size) {
        return socket.recv(size, ReceiveFlag.NONE);
    }

    static int recvBlocking(Socket socket, MemorySegment dst, int size) {
        return socket.recv(dst, 0, size, ReceiveFlag.NONE);
    }

    static void gatewaySendWithRetry(Gateway gateway, String service, byte[] payload, int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        try (Message msg = Message.fromBytes(payload)) {
            Message[] parts = new Message[]{msg};
            while (System.currentTimeMillis() < deadline) {
                try {
                    gateway.send(service, parts, SendFlag.NONE);
                    return;
                } catch (Exception ignored) {
                    sleep(10);
                }
            }
        }
        throw new RuntimeException("timeout");
    }

    static Spot.SpotMessage spotRecvWithTimeout(Spot spot, int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        while (System.currentTimeMillis() < deadline) {
            try {
                return spot.recv(ReceiveFlag.DONTWAIT);
            } catch (Exception ignored) {
                LockSupport.parkNanos(100_000L);
            }
        }
        throw new RuntimeException("timeout");
    }

    static Spot.SpotMessages spotRecvMessagesWithTimeout(Spot spot, int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        while (System.currentTimeMillis() < deadline) {
            try {
                return spot.recvMessages(ReceiveFlag.DONTWAIT);
            } catch (Exception ignored) {
                LockSupport.parkNanos(100_000L);
            }
        }
        throw new RuntimeException("timeout");
    }

    static Spot.SpotMessages spotRecvMessagesBlocking(Spot spot) {
        return spot.recvMessages(ReceiveFlag.NONE);
    }

    static Spot.SpotRawBorrowed spotRecvRawWithTimeout(
      Spot spot, Spot.RecvContext context, int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        while (System.currentTimeMillis() < deadline) {
            try {
                return spot.recvRawBorrowed(ReceiveFlag.DONTWAIT, context);
            } catch (Exception ignored) {
                LockSupport.parkNanos(100_000L);
            }
        }
        throw new RuntimeException("timeout");
    }

    static Spot.SpotRawBorrowed spotRecvRawBlocking(
      Spot spot, Spot.RecvContext context) {
        return spot.recvRawBorrowed(ReceiveFlag.NONE, context);
    }

    static int parseEnv(String name, int def) {
        String v = System.getenv(name);
        if (v == null || v.isEmpty()) {
            return def;
        }
        try {
            int p = Integer.parseInt(v);
            return p > 0 ? p : def;
        } catch (Exception ignored) {
            return def;
        }
    }

    static int resolveMsgCount(int size) {
        String v = System.getenv("BENCH_MSG_COUNT");
        if (v != null && !v.isEmpty()) {
            try {
                int p = Integer.parseInt(v);
                if (p > 0) {
                    return p;
                }
            } catch (Exception ignored) {
            }
        }
        return size <= 1024 ? 200000 : 20000;
    }

    static String endpointFor(String transport, String name) {
        if ("inproc".equals(transport)) {
            return "inproc://bench-" + name + "-" + System.currentTimeMillis();
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

    static void sleep(int ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException ignored) {
        }
    }

    record StreamFrame(byte[] rid, byte[] payload) {
    }

    interface Check {
        boolean get();
    }
}
