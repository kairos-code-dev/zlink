/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.Received;
import dev.kairoscode.zlink.ServiceMonitor;
import dev.kairoscode.zlink.ZlinkVersion;
import dev.kairoscode.zlink.service.spot.Spot;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

final class SampleSupport {
    private static final Duration TIMEOUT = Duration.ofSeconds(180);
    private static final long SPOT_FILTER_APPLIED = 1L << 13;

    private SampleSupport() {
    }

    static String tcpEndpoint() {
        try (ServerSocket socket = new ServerSocket(0)) {
            return "tcp://127.0.0.1:" + socket.getLocalPort();
        } catch (IOException ex) {
            throw new IllegalStateException("failed to allocate tcp port", ex);
        }
    }

    static void ensureNative() {
        ZlinkVersion.get();
    }

    static String inprocEndpoint(String prefix) {
        return "inproc://" + prefix + "-" + UUID.randomUUID();
    }

    static String uniqueTopic(String prefix) {
        return prefix + "." + UUID.randomUUID();
    }

    static void await(CountDownLatch latch, String label) {
        try {
            if (!latch.await(TIMEOUT.toMillis(), TimeUnit.MILLISECONDS)) {
                throw new IllegalStateException(label + " timed out");
            }
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException(label + " interrupted", ex);
        }
    }

    static void awaitThread(Thread thread, String label) {
        try {
            thread.join(TIMEOUT.toMillis());
            if (thread.isAlive()) {
                throw new IllegalStateException(label + " timed out");
            }
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException(label + " interrupted", ex);
        }
    }

    static Message wrapUtf8(String value) {
        ByteBuffer buffer = ByteBuffer.allocateDirect(value.length());
        buffer.put(value.getBytes(StandardCharsets.UTF_8));
        buffer.flip();
        return Message.wrapDirect(buffer);
    }

    static String singleUtf8(Received received) {
        return received.singlePartOrThrow().toUtf8String();
    }

    static void subscribeAndAwaitSpotFilterApplied(Spot spot, String topic) {
        try (ServiceMonitor subMonitor = spot.monitorOpen((int) SPOT_FILTER_APPLIED)) {
            CountDownLatch subReady = new CountDownLatch(1);
            subMonitor.onEvent(event -> {
                if ((event.eventType() & SPOT_FILTER_APPLIED) != 0) {
                    subReady.countDown();
                }
            });
            spot.setSubscription(topic);
            await(subReady, "spot filter applied");
        }
    }


    static java.net.Socket connectRawTcp(String endpoint) {
        try {
            InetSocketAddress address = tcpAddress(endpoint);
            java.net.Socket socket = new java.net.Socket();
            socket.connect(address, (int) TIMEOUT.toMillis());
            socket.setSoTimeout((int) TIMEOUT.toMillis());
            return socket;
        } catch (IOException ex) {
            throw new IllegalStateException("failed to connect raw tcp client", ex);
        }
    }

    static void sendRawTcp(java.net.Socket socket, byte[] payload) {
        try {
            OutputStream out = socket.getOutputStream();
            out.write(payload);
            out.flush();
        } catch (IOException ex) {
            throw new IllegalStateException("failed to send raw tcp payload", ex);
        }
    }

    static byte[] recvExactRawTcp(java.net.Socket socket, int length) {
        try {
            InputStream in = socket.getInputStream();
            return readExactly(in, length);
        } catch (IOException ex) {
            throw new IllegalStateException("failed to receive raw tcp payload", ex);
        }
    }

    private static InetSocketAddress tcpAddress(String endpoint) {
        if (!endpoint.startsWith("tcp://")) {
            throw new IllegalArgumentException(
                "raw tcp helper requires tcp:// endpoint: " + endpoint);
        }
        String hostPort = endpoint.substring("tcp://".length());
        int colon = hostPort.lastIndexOf(':');
        if (colon <= 0 || colon == hostPort.length() - 1) {
            throw new IllegalArgumentException("invalid tcp endpoint: " + endpoint);
        }
        String host = hostPort.substring(0, colon);
        int port = Integer.parseInt(hostPort.substring(colon + 1));
        return new InetSocketAddress(host, port);
    }

    private static byte[] readExactly(InputStream in, int length)
      throws IOException {
        byte[] data = new byte[length];
        int read = 0;
        while (read < length) {
            int n = in.read(data, read, length - read);
            if (n < 0) {
                throw new IOException("unexpected end of stream");
            }
            read += n;
        }
        return data;
    }

}
