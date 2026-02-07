package io.ulalax.zlink.integration;

import io.ulalax.zlink.Context;
import io.ulalax.zlink.Gateway;
import io.ulalax.zlink.Socket;
import io.ulalax.zlink.Spot;

import java.net.ServerSocket;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.function.BooleanSupplier;

final class TestTransports {
    static final int ZLINK_SUBSCRIBE = 6;
    static final int ZLINK_XPUB_VERBOSE = 40;
    static final int ZLINK_DONTWAIT = 1;
    static final int ZLINK_SNDMORE = 2;

    static final int ZLINK_PAIR = 0;
    static final int ZLINK_PUB = 1;
    static final int ZLINK_SUB = 2;
    static final int ZLINK_DEALER = 5;
    static final int ZLINK_ROUTER = 6;
    static final int ZLINK_XPUB = 9;
    static final int ZLINK_XSUB = 10;

    static class TransportCase {
        final String name;
        final String endpoint;

        TransportCase(String name, String endpoint) {
            this.name = name;
            this.endpoint = endpoint;
        }
    }

    static List<TransportCase> transports(String prefix) {
        List<TransportCase> cases = new ArrayList<>();
        cases.add(new TransportCase("tcp", ""));
        cases.add(new TransportCase("ws", ""));
        cases.add(new TransportCase("inproc", "inproc://" + prefix + "-" + Instant.now().toEpochMilli()));
        return cases;
    }

    static String endpointFor(String name, String baseEndpoint, String suffix) {
        if ("inproc".equals(name))
            return baseEndpoint + suffix;
        int port = getPort();
        return name + "://127.0.0.1:" + port;
    }

    static void tryTransport(String name, Runnable action) {
        try {
            action.run();
        } catch (RuntimeException ex) {
            if ("ws".equals(name))
                return;
            throw ex;
        }
    }

    static byte[] recvWithTimeout(Socket socket, int size, int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        RuntimeException last = null;
        while (System.currentTimeMillis() < deadline) {
            try {
                return socket.recv(size, ZLINK_DONTWAIT);
            } catch (RuntimeException ex) {
                last = ex;
                sleep(10);
            }
        }
        if (last != null)
            throw last;
        throw new RuntimeException("timeout");
    }

    static void sendWithRetry(Socket socket, byte[] data, int flags, int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        RuntimeException last = null;
        while (System.currentTimeMillis() < deadline) {
            try {
                socket.send(data, flags);
                return;
            } catch (RuntimeException ex) {
                last = ex;
                sleep(10);
            }
        }
        if (last != null)
            throw last;
        throw new RuntimeException("timeout");
    }

    static void gatewaySendWithRetry(Gateway gw, String service, byte[] payload,
      int flags, int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        RuntimeException last = null;
        while (System.currentTimeMillis() < deadline) {
            try {
                gw.send(service, new io.ulalax.zlink.Message[]{
                    io.ulalax.zlink.Message.fromBytes(payload)}, flags);
                return;
            } catch (RuntimeException ex) {
                last = ex;
                sleep(10);
            }
        }
        if (last != null)
            throw last;
        throw new RuntimeException("timeout");
    }

    static boolean waitUntil(BooleanSupplier check, int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        while (System.currentTimeMillis() < deadline) {
            try {
                if (check.getAsBoolean())
                    return true;
            } catch (RuntimeException ignored) {
            }
            sleep(10);
        }
        return false;
    }

    static Gateway.GatewayMessage gatewayReceiveWithTimeout(Gateway gw, int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        RuntimeException last = null;
        while (System.currentTimeMillis() < deadline) {
            try {
                return gw.recv(ZLINK_DONTWAIT);
            } catch (RuntimeException ex) {
                last = ex;
                sleep(10);
            }
        }
        if (last != null)
            throw last;
        throw new RuntimeException("timeout");
    }

    static Spot.SpotMessage spotReceiveWithTimeout(Spot spot, int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        RuntimeException last = null;
        while (System.currentTimeMillis() < deadline) {
            try {
                return spot.recv(ZLINK_DONTWAIT);
            } catch (RuntimeException ex) {
                last = ex;
                sleep(10);
            }
        }
        if (last != null)
            throw last;
        throw new RuntimeException("timeout");
    }

    private static void sleep(int ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException ignored) {
        }
    }

    private static int getPort() {
        try (ServerSocket socket = new ServerSocket(0)) {
            return socket.getLocalPort();
        } catch (Exception ex) {
            throw new RuntimeException(ex);
        }
    }

    static void closeContext(Context ctx) {
        Thread t = new Thread(ctx::close);
        t.setDaemon(true);
        t.start();
        try {
            t.join(3000);
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
    }

    private TestTransports() {}
}
