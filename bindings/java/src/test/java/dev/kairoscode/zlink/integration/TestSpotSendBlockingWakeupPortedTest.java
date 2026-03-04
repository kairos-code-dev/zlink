package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import dev.kairoscode.zlink.service.spot.SpotNodeOption;
import dev.kairoscode.zlink.service.spot.SpotNodePubMode;
import dev.kairoscode.zlink.service.spot.SpotNodeSocketRole;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestSpotSendBlockingWakeupPortedTest {
    private static final int SOCKET_HWM = 8;
    private static final int PROBE_WAIT_MS = 100;
    private static final int SEND_TIMEOUT_MS = 1200;
    private static final int TIMEOUT_SEND_MS = 500;
    private static final int RECV_TIMEOUT_MS = 200;
    private static final int READY_TIMEOUT_MS = 3000;
    private static final int MAX_FILL_MSGS = 20000;
    private static final int BACKPRESSURE_STREAK = 32;
    private static final int MSG_SIZE = 262144;
    private static final String TOPIC = "spot:blocking:wakeup";

    @Test
    public void testSpotBlockingSendWakesAfterReceiverDrains() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             SpotNode pubNode = new SpotNode(ctx);
             SpotNode subNode = new SpotNode(ctx);
             Arena arena = Arena.ofShared()) {
            configurePubNode(pubNode, 0);
            configureSubNode(subNode);

            String endpoint = TestSupport.inprocEndpoint("spot-send-wakeup");
            pubNode.bind(endpoint);
            subNode.connectPeerPub(endpoint);
            TestSupport.waitUntil(
                () -> !pubNode.pubPeers().isEmpty() || !subNode.subPeers().isEmpty(),
                TestSupport.DEFAULT_TIMEOUT_MS,
                "spot peer connection not established");

            try (Spot spotPub = new Spot(pubNode);
                 Spot spotSub = new Spot(subNode)) {
                spotSub.subscribe(TOPIC);

                ReadyProbeResult ready = waitUntilReady(spotPub, spotSub, arena);
                assertTrue(ready.ready(),
                    "spot topology not ready: " + ready);

                int filled = fillUntilEagain(spotPub, arena);
                assertTrue(filled > 0);
                assertBackpressuredNow(spotPub, arena);
                TestSupport.sleepMs(PROBE_WAIT_MS);
                assertBackpressuredNow(spotPub, arena);

                pubNode.setSockOpt(SpotNodeSocketRole.PUB, SocketOption.SNDTIMEO,
                    SEND_TIMEOUT_MS);

                MemorySegment payload = payload(arena, 'a');
                ExecutorService executor = Executors.newSingleThreadExecutor();
                try {
                    Future<Integer> sendFuture = executor.submit(() -> {
                        try {
                            publishPayload(spotPub, payload);
                            return 0;
                        } catch (RuntimeException ex) {
                            return -1;
                        }
                    });

                    boolean blockedBeforeDrain;
                    try {
                        sendFuture.get(PROBE_WAIT_MS, TimeUnit.MILLISECONDS);
                        blockedBeforeDrain = false;
                    } catch (TimeoutException ex) {
                        blockedBeforeDrain = true;
                    }

                    for (int i = 0; i < SOCKET_HWM * 16; i++) {
                        if (sendFuture.isDone())
                            break;
                        try {
                            spotSub.recv(ReceiveFlag.DONTWAIT);
                        } catch (RuntimeException ex) {
                            TestSupport.sleepMs(1);
                        }
                    }

                    int rc = sendFuture.get(SEND_TIMEOUT_MS + 500L,
                        TimeUnit.MILLISECONDS);
                    assertTrue(blockedBeforeDrain);
                    assertEquals(0, rc);
                } finally {
                    executor.shutdownNow();
                }
            }
        }
    }

    @Test
    public void testSpotBlockingSendTimesOutWithoutReceiverReads() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             SpotNode pubNode = new SpotNode(ctx);
             SpotNode subNode = new SpotNode(ctx);
             Arena arena = Arena.ofShared()) {
            configurePubNode(pubNode, 0);
            configureSubNode(subNode);

            String endpoint = TestSupport.inprocEndpoint("spot-send-timeout");
            pubNode.bind(endpoint);
            subNode.connectPeerPub(endpoint);
            TestSupport.waitUntil(
                () -> !pubNode.pubPeers().isEmpty() || !subNode.subPeers().isEmpty(),
                TestSupport.DEFAULT_TIMEOUT_MS,
                "spot peer connection not established");

            try (Spot spotPub = new Spot(pubNode);
                 Spot spotSub = new Spot(subNode)) {
                spotSub.subscribe(TOPIC);

                ReadyProbeResult ready = waitUntilReady(spotPub, spotSub, arena);
                assertTrue(ready.ready(),
                    "spot topology not ready: " + ready);

                int filled = fillUntilEagain(spotPub, arena);
                assertTrue(filled > 0);
                assertBackpressuredNow(spotPub, arena);

                pubNode.setSockOpt(SpotNodeSocketRole.PUB, SocketOption.SNDTIMEO,
                    TIMEOUT_SEND_MS);

                long start = System.nanoTime();
                try {
                    publishPayload(spotPub, payload(arena, 'b'));
                } catch (RuntimeException expected) {
                    long elapsedMs = (System.nanoTime() - start) / 1_000_000L;
                    assertTrue(elapsedMs >= TIMEOUT_SEND_MS - 120,
                        "elapsedMs=" + elapsedMs);
                    return;
                }
                throw new AssertionError("expected send timeout");
            }
        }
    }

    private static void configurePubNode(SpotNode node, int sndtimeoMs) {
        node.setSockOpt(SpotNodeOption.PUB_MODE, SpotNodePubMode.SYNC.getValue());
        node.setSockOpt(SpotNodeSocketRole.PUB, SocketOption.SNDHWM, SOCKET_HWM);
        node.setSockOpt(SpotNodeSocketRole.PUB, SocketOption.LINGER, 0);
        node.setSockOpt(SpotNodeSocketRole.PUB, SocketOption.XPUB_NODROP, 1);
        node.setSockOpt(SpotNodeSocketRole.PUB, SocketOption.SNDTIMEO, sndtimeoMs);
    }

    private static void configureSubNode(SpotNode node) {
        node.setSockOpt(SpotNodeSocketRole.SUB, SocketOption.RCVHWM, SOCKET_HWM);
        node.setSockOpt(SpotNodeSocketRole.SUB, SocketOption.RCVTIMEO,
            RECV_TIMEOUT_MS);
        node.setSockOpt(SpotNodeSocketRole.SUB, SocketOption.LINGER, 0);
        node.setSockOpt(SpotNodeSocketRole.SUB, SocketOption.XPUB_NODROP, 1);
    }

    private static ReadyProbeResult waitUntilReady(Spot spotPub, Spot spotSub,
                                                   Arena arena) {
        MemorySegment probePayload = arena.allocateFrom("R",
            StandardCharsets.UTF_8);
        long deadline = System.currentTimeMillis() + READY_TIMEOUT_MS;
        int sendOk = 0;
        int sendFail = 0;
        int recvOk = 0;
        int recvFail = 0;
        while (System.currentTimeMillis() < deadline) {
            try {
                publishPayload(spotPub, probePayload, 0, 1);
                sendOk++;
            } catch (RuntimeException ex) {
                sendFail++;
                TestSupport.sleepMs(1);
                continue;
            }

            try {
                spotSub.recv(ReceiveFlag.DONTWAIT);
                recvOk++;
                return new ReadyProbeResult(true, sendOk, sendFail, recvOk,
                    recvFail);
            } catch (RuntimeException ex) {
                recvFail++;
                TestSupport.sleepMs(1);
            }
        }
        return new ReadyProbeResult(false, sendOk, sendFail, recvOk, recvFail);
    }

    private static int fillUntilEagain(Spot spotPub, Arena arena) {
        MemorySegment payload = payload(arena, 'f');
        int sent = 0;
        int eagainStreak = 0;
        for (int i = 0; i < MAX_FILL_MSGS; i++) {
            try {
                publishPayload(spotPub, payload);
                sent++;
                eagainStreak = 0;
            } catch (RuntimeException ex) {
                eagainStreak++;
                if (eagainStreak >= BACKPRESSURE_STREAK && sent > 0)
                    return sent;
            }
        }
        return -1;
    }

    private static void assertBackpressuredNow(Spot spotPub, Arena arena) {
        MemorySegment payload = payload(arena, 'p');
        for (int i = 0; i < BACKPRESSURE_STREAK; i++) {
            try {
                publishPayload(spotPub, payload);
                throw new AssertionError("expected backpressure");
            } catch (RuntimeException expected) {
            }
        }
    }

    private static void publishPayload(Spot spotPub, MemorySegment payload) {
        publishPayload(spotPub, payload, 0, payload.byteSize());
    }

    private static void publishPayload(Spot spotPub, MemorySegment payload,
                                       long offset, long length) {
        try (Message msg = Message.fromMemorySegment(payload, offset, length)) {
            spotPub.publish(TOPIC, msg, SendFlag.NONE);
        }
    }

    private static MemorySegment payload(Arena arena, char ch) {
        byte[] out = new byte[MSG_SIZE];
        byte fill = (byte) ch;
        for (int i = 0; i < out.length; i++) {
            out[i] = fill;
        }
        MemorySegment seg = arena.allocate(out.length);
        MemorySegment.copy(MemorySegment.ofArray(out), 0, seg, 0, out.length);
        return seg;
    }

    private record ReadyProbeResult(boolean ready, int sendOk, int sendFail,
                                    int recvOk, int recvFail) {
    }
}
