/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.single;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.RecordKind;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SubscriptionKind;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.perf.PerfMeshDispatch;
import systems.zlink.perf.PerfUtil;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.List;

final class PerfSpot {
    private static final String CHANNEL = "bench-single-spot";

    private PerfSpot() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        if ("tls".equals(config.transport()) || "wss".equals(config.transport())) {
            return PerfUtil.Result.unsupported(
                "MeshNode benchmark trust profiles are not configured", config);
        }
        String topic = "perf.topic." + System.nanoTime();
        String publisherEndpoint = PerfUtil.endpoint(
            config.transport(), "single-spot-pub");
        String subscriberEndpoint = PerfUtil.endpoint(
            config.transport(), "single-spot-sub");
        RoutingId publisherRid = routingId("z-java-perf-mesh-publisher");
        try (var ctx = PerfUtil.newContext(config);
             MeshNode publisherNode = ctx.createMeshNode();
             MeshNode subscriberNode = ctx.createMeshNode()) {
            publisherNode.setRoutingId(publisherRid);
            publisherNode.setBind(publisherEndpoint);
            publisherNode.addChannel(CHANNEL);
            publisherNode.start();
            subscriberNode.setRoutingId(routingId("a-java-perf-mesh-subscriber"));
            subscriberNode.setBind(subscriberEndpoint);
            subscriberNode.addChannel(CHANNEL);
            subscriberNode.start();
            subscriberNode.connectPeer(publisherEndpoint, publisherRid);
            try (Spot publisher = publisherNode.createSpot();
                 Spot subscriber = subscriberNode.createSpot();
                 PerfMeshDispatch dispatch = new PerfMeshDispatch(subscriberNode, 16)) {
                subscriber.setSubscription(CHANNEL, topic, SubscriptionKind.EXACT);
                waitForPeer(subscriberNode);
                PerfUtil.recalculateAutoHwm(ctx);
                PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
                long activeEnd = System.nanoTime()
                    + Duration.ofSeconds(config.durationSeconds()).toNanos();
                while (System.nanoTime() < activeEnd) {
                    try (Message payload = PerfUtil.payload(
                        config.size(), (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                        try {
                            publisher.publish(
                                CHANNEL, topic, List.of(payload), SendFlags.DONT_WAIT);
                        } catch (ZlinkSubmitException error) {
                            if (!transientSubmit(error)) {
                                throw error;
                            }
                        }
                    }
                    dispatch.drain((owner, record, parts) -> {
                        if (record.kind() == RecordKind.SPOT_MULTICAST
                            && !parts.isEmpty()
                            && PerfUtil.recordActiveLatency(
                                metrics, parts.get(0), config.size(), false)) {
                            metrics.recordEvent();
                        }
                    });
                }
                return metrics.finishSingle(config);
            }
        }
    }

    private static boolean transientSubmit(ZlinkSubmitException error) {
        return error.getResult() == SubmitResult.BACKPRESSURED
            || error.getResult() == SubmitResult.NOT_CONNECTED;
    }

    private static void waitForPeer(MeshNode node) {
        long deadline = System.nanoTime() + Duration.ofSeconds(10).toNanos();
        while (System.nanoTime() < deadline) {
            if (node.status().admittedPeerCount() > 0) {
                return;
            }
            try {
                Thread.sleep(10);
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new IllegalStateException("mesh peer wait interrupted", error);
            }
        }
        throw new IllegalStateException("mesh peer admission timed out");
    }

    private static RoutingId routingId(String value) {
        return RoutingId.from(value.getBytes(StandardCharsets.UTF_8));
    }
}
