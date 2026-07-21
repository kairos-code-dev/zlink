/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.Claim;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.MeshNodePublisher;
import systems.zlink.contracts.service.spot.ReadyBatch;
import systems.zlink.contracts.service.spot.ReadyDomain;
import systems.zlink.contracts.service.spot.ReceiveBatch;
import systems.zlink.contracts.service.spot.ReceiveRecord;
import systems.zlink.contracts.service.spot.RecordKind;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SubscriptionKind;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.perf.PerfUtil;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;

final class PerfSpotDirectControl implements AutoCloseable {
    private static final String CHANNEL = "bench-control";
    private static final String TOPIC = "bench";

    private final MeshNode node;
    private final MeshNodePublisher publisher;
    private final Spot subscriber;
    private final ReadyBatch ready = ReadyBatch.create(8);
    private final ReceiveBatch received = ReceiveBatch.create(32, 64, 1 << 16);

    private PerfSpotDirectControl(
        MeshNode node,
        MeshNodePublisher publisher,
        Spot subscriber) {
        this.node = node;
        this.publisher = publisher;
        this.subscriber = subscriber;
    }

    static PerfSpotDirectControl bind(
        Context ctx,
        PerfUtil.Config config,
        String endpoint,
        String label) {
        MeshNode node = ctx.createMeshNode();
        node.setRoutingId(routingId(label + "-control-node"));
        node.setBind(endpoint);
        node.addChannel(CHANNEL);
        node.start();
        Spot subscriber = node.createSpot();
        subscriber.setSubscription(CHANNEL, TOPIC, SubscriptionKind.EXACT);
        return new PerfSpotDirectControl(
            node,
            node.createPublisher(),
            subscriber);
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

    ReadyState waitReady(
        int size,
        int expectedCount,
        int timeoutMs,
        Consumer<String> dataEndpointHandler) {
        long deadline = System.nanoTime()
            + Duration.ofMillis(Math.max(1, timeoutMs)).toNanos();
        int readyCount = 0;
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
                if (parts.length == 3 && Integer.parseInt(parts[1]) == size) {
                    readyCount += Integer.parseInt(parts[2]);
                    if (readyCount >= expectedCount) {
                        return new ReadyState(dataEndpoints);
                    }
                }
            }
        }
        throw new IllegalStateException("mesh control ready_count timed out");
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
        throw new IllegalStateException("mesh control start timed out");
    }

    private void publish(String payload) {
        try (Message message = Message.from(payload.getBytes(StandardCharsets.UTF_8))) {
            publisher.publish(CHANNEL, TOPIC, List.of(message), SendFlags.NONE);
        }
    }

    private synchronized String recvPayload() {
        ready.reset();
        node.drainReady(
            ReadyDomain.mask(ReadyDomain.APPLICATION),
            ready,
            RecvFlags.DONT_WAIT);
        for (int i = 0; i < ready.count(); i++) {
            if (!subscriber.routingId().equals(ready.at(i).spotRid())) {
                continue;
            }
            try (Claim claim = ready.takeClaim(i)) {
                while (claim.valid()) {
                    received.reset();
                    if (claim.recvBatch(received, RecvFlags.DONT_WAIT).resultCode() != 0) {
                        break;
                    }
                    for (int r = 0; r < received.count(); r++) {
                        ReceiveRecord record = received.at(r);
                        if (record.kind() != RecordKind.SPOT_MULTICAST
                            || !CHANNEL.equals(record.channelName())
                            || !TOPIC.equals(record.topic())) {
                            continue;
                        }
                        List<Message> parts = received.retainMessage(r);
                        try {
                            return parts.isEmpty() ? null : parts.get(0).toUtf8String();
                        } finally {
                            parts.forEach(Message::close);
                        }
                    }
                }
            }
        }
        return null;
    }

    private static void sleepQuietly(int millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("mesh control interrupted", ex);
        }
    }

    @Override
    public void close() {
        received.close();
        ready.close();
        subscriber.close();
        publisher.close();
        node.close();
    }

    private static RoutingId routingId(String value) {
        return RoutingId.from(value.getBytes(StandardCharsets.UTF_8));
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
