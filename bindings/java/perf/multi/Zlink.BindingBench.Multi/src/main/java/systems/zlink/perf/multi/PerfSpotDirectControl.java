/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Context;
import systems.zlink.contracts.Message;
import systems.zlink.contracts.RecvFlags;
import systems.zlink.contracts.RoutingId;
import systems.zlink.contracts.SendFlags;
import systems.zlink.contracts.TopicMessage;
import systems.zlink.perf.PerfUtil;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotNode;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;

final class PerfSpotDirectControl implements AutoCloseable {
    private static final String TOPIC = "bench";

    private final SpotNode node;
    private final Spot pub;
    private final Spot sub;

    private PerfSpotDirectControl(SpotNode node, Spot pub, Spot sub) {
        this.node = node;
        this.pub = pub;
        this.sub = sub;
    }

    static PerfSpotDirectControl bind(Context ctx, PerfUtil.Config config,
                                      String endpoint, String label) {
        SpotNode node = new SpotNode(ctx);
        Spot pub = node.createSpot();
        Spot sub = node.createSpot();
        node.setRoutingId(routingId(label + "-control-node"));
        pub.setRoutingId(routingId(label + "-control-pub"));
        sub.setRoutingId(routingId(label + "-control-sub"));
        PerfUtil.configureServerTls(node, config.transport());
        PerfUtil.configureClientTls(node, config.transport());
        node.bind(endpoint);
        sub.setSubscription(TOPIC);
        return new PerfSpotDirectControl(node, pub, sub);
    }

    void connectPeer(String endpoint) {
        node.connectPeer(endpoint);
    }

    void publishConnected() {
        publish("CONNECTED");
    }

    void publishDataEndpoint(String endpoint) {
        publish("DATA_ENDPOINT," + endpoint);
    }

    void publishReadyCount(int size, int count) {
        publish("READY_COUNT," + size + "," + count);
    }

    void publishStart(int size) {
        publish("START," + size);
    }

    void waitReadyCount(int size, int expectedCount, int timeoutMs) {
        waitReady(size, expectedCount, timeoutMs);
    }

    ReadyState waitReady(int size, int expectedCount, int timeoutMs) {
        return waitReady(size, expectedCount, timeoutMs, null);
    }

    ReadyState waitReady(int size, int expectedCount, int timeoutMs,
                         Consumer<String> dataEndpointHandler) {
        long deadline = System.nanoTime()
            + Duration.ofMillis(Math.max(1, timeoutMs)).toNanos();
        int ready = 0;
        List<String> dataEndpoints = new ArrayList<>();
        while (System.nanoTime() < deadline) {
            String payload = recvPayload();
            if (payload == null) {
                sleepQuietly(1);
                continue;
            }
            if (payload.startsWith("DATA_ENDPOINT,")) {
                String endpoint = payload.substring("DATA_ENDPOINT,".length());
                dataEndpoints.add(endpoint);
                if (dataEndpointHandler != null) {
                    dataEndpointHandler.accept(endpoint);
                }
            } else if (payload.startsWith("READY_COUNT,")) {
                String[] parts = payload.split(",", 3);
                if (parts.length == 3
                    && Integer.parseInt(parts[1]) == size) {
                    ready += Integer.parseInt(parts[2]);
                    if (ready >= expectedCount) {
                        return new ReadyState(dataEndpoints);
                    }
                }
            }
        }
        throw new IllegalStateException("spot direct ready_count timed out");
    }

    void waitStart(int size, int timeoutMs) {
        long deadline = System.nanoTime()
            + Duration.ofMillis(Math.max(1, timeoutMs)).toNanos();
        String expected = "START," + size;
        while (System.nanoTime() < deadline) {
            String payload = recvPayload();
            if (expected.equals(payload)) {
                return;
            }
            if (payload == null) {
                sleepQuietly(1);
            }
        }
        throw new IllegalStateException("spot direct start timed out");
    }

    private void publish(String payload) {
        try (Message message = Message.copyOf(payload.getBytes(StandardCharsets.UTF_8))) {
            pub.publish(TOPIC)
                .message(message)
                .flags(SendFlags.NONE)
                .submit();
        }
    }

    private String recvPayload() {
        try (TopicMessage message = new TopicMessage()) {
            if (!sub.subscribe(message, RecvFlags.DONT_WAIT)
                || message.parts().isEmpty()) {
                return null;
            }
            return message.firstPart().toUtf8String();
        }
    }

    private static void sleepQuietly(int millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("spot direct control interrupted",
                ex);
        }
    }

    @Override
    public void close() {
        sub.close();
        pub.close();
        node.close();
    }

    private static RoutingId routingId(String value) {
        return RoutingId.fromBytes(value.getBytes(StandardCharsets.UTF_8));
    }

    static final class ReadyState {
        private final List<String> dataEndpoints;

        private ReadyState(List<String> dataEndpoints) {
            this.dataEndpoints = List.copyOf(dataEndpoints);
        }

        List<String> dataEndpoints() {
            return dataEndpoints;
        }
    }
}
