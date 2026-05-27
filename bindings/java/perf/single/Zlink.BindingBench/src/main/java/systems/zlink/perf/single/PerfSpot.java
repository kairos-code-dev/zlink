/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.single;

import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.eventing.PollEventFlag;
import systems.zlink.contracts.eventing.PollEvents;
import systems.zlink.contracts.eventing.Poller;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.service.registry.Registry;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.messaging.TopicMessage;
import systems.zlink.perf.PerfStopToken;
import systems.zlink.perf.PerfUtil;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

final class PerfSpot {
    private PerfSpot() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String topic = "perf.topic." + System.nanoTime();
        String publisherEndpoint = normalizeSpotEndpoint(
            PerfUtil.endpoint(config.transport(), "single-spot-pub"),
            config.transport());

        try (var ctx = PerfUtil.newContext(config);
             SpotNode publisherNode = new SpotNode(ctx);
             SpotNode subscriberNode = new SpotNode(ctx);
             Spot publisher = publisherNode.createSpot();
             Spot stopPublisher = subscriberNode.createSpot();
             Spot subscriber = subscriberNode.createSpot()) {
            PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
            PerfUtil.configureServerTls(publisherNode, config.transport());
            PerfUtil.configureClientTls(publisherNode, config.transport());
            PerfUtil.configureServerTls(subscriberNode, config.transport());
            PerfUtil.configureClientTls(subscriberNode, config.transport());
            publisherNode.setRoutingId(routingId("z-java-perf-spot-publisher"));
            subscriberNode.setRoutingId(routingId("a-java-perf-spot-subscriber"));
            publisher.setRoutingId(routingId("z-java-perf-spot-publisher-spot"));
            stopPublisher.setRoutingId(routingId("a-java-perf-spot-stop-spot"));
            subscriber.setRoutingId(routingId("a-java-perf-spot-subscriber-spot"));
            // C parity: perf_spot.cpp run_case (~669-690) and
            // bindings/cpp/perf/single/src/perf_spot.cpp (~387-397):
            // publisher bind -> subscriber direct connect_peer ->
            // set_subscription -> local pub/sub probe ready barrier. No
            // Registry/Discovery and no peer-connected status wait: the
            // local probe barrier below is the connection-ready gate
            // (PERF_SINGLE §6.1 / PERF_POLICY §1.1.2: single SPOT must not
            // measure registry/discovery or extra bootstrap waits).
            publisherNode.setPubBind(publisherEndpoint);
            subscriberNode.connectPeer(publisherEndpoint);
            subscriber.setSubscription(topic);

            waitForReadyProbe(publisher, subscriber, config, topic, metrics);
            settleAfterReady();
            PerfUtil.recalculateAutoHwm(ctx);

            long activeEnd = System.nanoTime()
                + config.durationSeconds() * 1_000_000_000L;
            CountDownLatch stopped = new CountDownLatch(1);
            AtomicReference<Throwable> failure = new AtomicReference<>();
            Thread receiverThread = new Thread(() -> drainUntilStopToken(
                subscriber, config, metrics, activeEnd, stopped, failure),
                "single-spot-receiver");
            Thread senderThread = new Thread(() -> publishActiveAndStop(
                publisher, stopPublisher, config, topic, activeEnd,
                stopped, failure),
                "single-spot-sender");
            receiverThread.start();
            senderThread.start();
            PerfUtil.await(stopped, "spot receiver",
                Duration.ofSeconds(config.durationSeconds()
                    + PerfUtil.intEnv("PERF_SINGLE_SPOT_STOP_GRACE_SECONDS", 120)));
            PerfUtil.join(senderThread, "spot sender", Duration.ofSeconds(10));
            PerfUtil.join(receiverThread, "spot receiver thread",
                Duration.ofSeconds(10));
            if (failure.get() != null) {
                throw new IllegalStateException("spot active phase failed",
                    failure.get());
            }
            PerfUtil.printSingleSpotNodeAutoHwm(config, publisherNode,
                "publisher");
            PerfUtil.printSingleSpotNodeAutoHwm(config, subscriberNode,
                "subscriber");

            return metrics.finishSingle(config);
        }
    }

    private static void waitForReadyProbe(Spot publisher, Spot subscriber,
                                          PerfUtil.Config config, String topic,
                                          PerfUtil.Metrics metrics) {
        long deadline = System.nanoTime() + Duration.ofSeconds(10).toNanos();
        while (System.nanoTime() < deadline) {
            try (Message probe = PerfUtil.payload(config.size(),
                     (byte) PerfUtil.PHASE_WARMUP, System.nanoTime())) {
                publisher.publish(topic)
                    .message(probe)
                    .flags(SendFlags.DONT_WAIT)
                    .submit();
            }
            if (drainSubscriber(subscriber, config, metrics, Long.MAX_VALUE,
                    false)) {
                return;
            }
            sleepQuietly(Duration.ofMillis(10), "spot local probe interrupted");
        }
        throw new IllegalStateException("spot local probe ready timeout");
    }

    private static boolean drainSubscriber(Spot subscriber,
                                           PerfUtil.Config config,
                                           PerfUtil.Metrics metrics,
                                           long activeEnd,
                                           boolean countActive) {
        boolean processed = false;
        while (true) {
            var maybe = PerfUtil.subscribeNoWait(subscriber);
            if (maybe.isEmpty()) {
                return processed;
            }
            try (TopicMessage subscribed = maybe.orElseThrow()) {
                if (PerfStopToken.isStopTokenMessage(subscribed.firstPart())) {
                    processed = true;
                    continue;
                }
                PerfUtil.Header header = PerfUtil.decodeHeader(
                    subscribed.firstPart(), config.size());
                if (header == null) {
                    processed = true;
                    continue;
                }
                if (header.phase() == PerfUtil.PHASE_WARMUP) {
                    processed = true;
                    return true;
                }
                if (!countActive
                    || header.phase() != PerfUtil.PHASE_ACTIVE
                    || System.nanoTime() > activeEnd) {
                    processed = true;
                    continue;
                }
                metrics.recordNanos(header.latencyNanos());
                processed = true;
            }
        }
    }

    /**
     * Drain until the wire-level stop token is observed. PERF_SINGLE_TEST_POLICY
     * § 1.4 mandates that phase-end is signaled by the stop token.
     */
    private static void drainUntilStopToken(Spot subscriber,
                                            PerfUtil.Config config,
                                            PerfUtil.Metrics metrics,
                                            long activeEnd,
                                            CountDownLatch stopped,
                                            AtomicReference<Throwable> failure) {
        try (Poller poller = new Poller()) {
            poller.add(subscriber, 0L, PollEventFlag.POLLIN);
            while (true) {
                waitFor(poller, PollEventFlag.POLLIN);
                while (true) {
                    try (TopicMessage subscribed = new TopicMessage()) {
                        if (!subscriber.subscribe(subscribed, RecvFlags.DONT_WAIT)) {
                            break;
                        }
                        if (PerfStopToken.isStopTokenMessage(
                                subscribed.firstPart())) {
                            stopped.countDown();
                            return;
                        }
                        PerfUtil.Header header = PerfUtil.decodeHeader(
                            subscribed.firstPart(), config.size());
                        if (header == null) {
                            continue;
                        }
                        if (header.phase() == PerfUtil.PHASE_ACTIVE) {
                            if (System.nanoTime() <= activeEnd) {
                                metrics.recordNanos(header.latencyNanos());
                            }
                        }
                    }
                }
            }
        } catch (Throwable ex) {
            failure.compareAndSet(null, ex);
            stopped.countDown();
        }
    }

    private static void publishActiveAndStop(Spot publisher,
                                             Spot stopPublisher,
                                             PerfUtil.Config config,
                                             String topic,
                                             long activeEnd,
                                             CountDownLatch stopped,
                                             AtomicReference<Throwable> failure) {
        try (Message active = PerfUtil.payloadTemplate(config.size())) {
            while (System.nanoTime() < activeEnd) {
                PerfUtil.resetAndWritePayload(active, config.size(),
                    (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime());
                if (!publishWhenWritableUntil(publisher, topic, active, activeEnd)) {
                    break;
                }
            }
            try (Message stop = PerfStopToken.newMessage()) {
                stopPublisher.publish(topic)
                    .message(stop)
                    .flags(SendFlags.NONE)
                    .submit();
            }
        } catch (Throwable ex) {
            failure.compareAndSet(null, ex);
            stopped.countDown();
        }
    }

    private static void publishWhenWritable(Spot publisher,
                                            String topic,
                                            Message message) {
        while (!tryPublishWhenWritable(publisher, topic, message)) {
            sleepQuietly(Duration.ofMillis(1), "spot publish backpressure");
        }
    }

    private static boolean publishWhenWritableUntil(Spot publisher,
                                                    String topic,
                                                    Message message,
                                                    long deadlineNanos) {
        // C parity (perf_spot.cpp send_spot_samples): nonblocking publish; on
        // backpressure do a fixed 1ms poll-sleep (perf_socket_poll(NULL,0,1))
        // then retry, rather than a socket POLLOUT wait.
        while (System.nanoTime() < deadlineNanos) {
            if (tryPublishWhenWritable(publisher, topic, message)) {
                return true;
            }
            sleepQuietly(Duration.ofMillis(1), "spot publish backpressure");
        }
        return false;
    }

    private static boolean tryPublishWhenWritable(Spot publisher,
                                                 String topic,
                                                 Message message) {
        return publisher.publish(topic)
            .message(message)
            .flags(SendFlags.DONT_WAIT)
            .submit();
    }

    private static void waitFor(Poller poller, PollEventFlag expected) {
        PollEvents events = new PollEvents(1);
        for (;;) {
            int count = poller.wait(events, Duration.ofMillis(-1));
            for (int i = 0; i < count; i++)
                if (events.hasEvent(i, expected))
                    return;
        }
    }

    private static void settleAfterReady() {
        int settleMs = PerfUtil.intEnv("PERF_SINGLE_SPOT_READY_SETTLE_MS", 1000);
        if (settleMs <= 0) {
            return;
        }
        sleepQuietly(Duration.ofMillis(settleMs), "spot settle interrupted");
    }

    private static void sleepQuietly(Duration duration, String label) {
        try {
            Thread.sleep(duration.toMillis());
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException(label, ex);
        }
    }

    private static String normalizeSpotEndpoint(String endpoint, String transport) {
        if (!"tls".equals(transport) && !"wss".equals(transport)) {
            return endpoint;
        }
        return endpoint.replace("://127.0.0.1:", "://localhost:");
    }

    private static RoutingId routingId(String value) {
        return RoutingId.from(value.getBytes(StandardCharsets.UTF_8));
    }
}
