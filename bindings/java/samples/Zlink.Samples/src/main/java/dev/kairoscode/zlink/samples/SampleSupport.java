/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.MonitorSocket;
import dev.kairoscode.zlink.Received;
import dev.kairoscode.zlink.ServiceMonitor;
import dev.kairoscode.zlink.ServiceEvent;
import dev.kairoscode.zlink.ZlinkVersion;
import dev.kairoscode.zlink.service.spot.Spot;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.time.Duration;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

final class SampleSupport {
    private static final Duration TIMEOUT = Duration.ofSeconds(180);
    private static final long SPOT_FILTER_APPLIED = 1L << 13;
    static final String PAIR_PAYLOAD = "hello-pair";
    static final String DEALER_REQUEST = "ping";
    static final String DEALER_REPLY = "pong";
    static final String STREAM_PAYLOAD = "hello-stream";
    static final String PUBSUB_TOPIC = "prices";
    static final String PUBSUB_PAYLOAD = "101.25";
    static final String SPOT_TOPIC = "room:lobby";
    static final String SPOT_PAYLOAD = "hello-spot";

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

    static String singleUtf8(Received received) {
        return received.singlePartOrThrow().toUtf8String();
    }

    static final int CONNECTION_READY_EVENT =
        MonitorEventType.CONNECTION_READY.getValue();
    static final int STREAM_READY_EVENTS =
        MonitorEventType.ACCEPTED.getValue()
            | MonitorEventType.CONNECTION_READY.getValue();
    static final int PUBSUB_READY_EVENTS =
        MonitorEventType.CONNECTION_READY.getValue();

    static void waitConnected(MonitorSocket... monitors) {
        for (MonitorSocket monitor : monitors) {
            monitor.recv();
        }
    }

    static void waitStreamConnected(MonitorSocket monitor) {
        while (true) {
            var event = monitor.recv();
            if (event.event() == MonitorEventType.ACCEPTED.getValue()
                || event.event() == MonitorEventType.CONNECTION_READY.getValue()) {
                return;
            }
        }
    }

    static void waitPubSubReady(MonitorSocket pubMonitor,
                                MonitorSocket subMonitor) {
        waitMonitorEvent(subMonitor,
            MonitorEventType.CONNECTION_READY.getValue());
        waitMonitorEvent(pubMonitor,
            MonitorEventType.CONNECTION_READY.getValue());
    }

    static ServiceEvent awaitSpotFilterApplied(Spot spot, String topic) {
        try (ServiceMonitor monitor = spot.monitorOpen((int) SPOT_FILTER_APPLIED)) {
            if (monitor.tryRecv().isPresent()) {
                throw new IllegalStateException(
                    "spot monitor unexpectedly had a pending event");
            }
            spot.setSubscription(topic);
            ServiceEvent event = monitor.recv();
            if ((event.eventType() & SPOT_FILTER_APPLIED) == 0) {
                throw new IllegalStateException(
                    "expected filter-applied event but got " + event.eventType());
            }
            if (!topic.equals(event.subject())) {
                throw new IllegalStateException(
                    "unexpected monitor subject: " + event.subject());
            }
            return event;
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

    private static void waitMonitorEvent(MonitorSocket monitor, int eventType) {
        while (true) {
            var event = monitor.recv();
            if (event.event() == eventType && event.value() > 0) {
                return;
            }
        }
    }

}
