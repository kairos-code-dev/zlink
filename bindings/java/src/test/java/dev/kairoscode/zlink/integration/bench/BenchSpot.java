package dev.kairoscode.zlink.integration.bench;

import dev.kairoscode.zlink.*;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

final class BenchSpot {
    private BenchSpot() {
    }

    static int run(String transport, int size) {
        int warmup = BenchUtil.parseEnv("BENCH_WARMUP_COUNT", 200);
        int latCount = BenchUtil.parseEnv("BENCH_LAT_COUNT", 200);
        int msgCount = BenchUtil.resolveMsgCount(size);
        int maxSpot = BenchUtil.parseEnv("BENCH_SPOT_MSG_COUNT_MAX", 50000);
        if (msgCount > maxSpot) {
            msgCount = maxSpot;
        }

        Context ctx = new Context();
        SpotNode nodePub = null;
        SpotNode nodeSub = null;
        Spot spotPub = null;
        Spot spotSub = null;
        Arena payloadArena = null;
        try {
            nodePub = new SpotNode(ctx);
            nodeSub = new SpotNode(ctx);
            String endpoint = BenchUtil.endpointFor(transport, "spot");
            nodePub.bind(endpoint);
            nodeSub.connectPeerPub(endpoint);
            spotPub = new Spot(nodePub);
            spotSub = new Spot(nodeSub);
            spotSub.subscribe("bench");
            BenchUtil.sleep(300);

            byte[] payload = new byte[size];
            for (int i = 0; i < size; i++) {
                payload[i] = 'a';
            }
            payloadArena = Arena.ofShared();
            MemorySegment payloadSegment = payloadArena.allocate(size);
            MemorySegment.copy(MemorySegment.ofArray(payload), 0, payloadSegment, 0, size);
            Message[] sendParts = new Message[1];

            for (int i = 0; i < warmup; i++) {
                try (Message msg = Message.fromNativeData(payloadSegment)) {
                    sendParts[0] = msg;
                    spotPub.publishMove("bench", sendParts, SendFlag.NONE);
                } finally {
                    sendParts[0] = null;
                }
                try (Spot.SpotMessages ignored =
                       BenchUtil.spotRecvMessagesWithTimeout(spotSub, 5000)) {
                }
            }

            long t0 = System.nanoTime();
            for (int i = 0; i < latCount; i++) {
                try (Message msg = Message.fromNativeData(payloadSegment)) {
                    sendParts[0] = msg;
                    spotPub.publishMove("bench", sendParts, SendFlag.NONE);
                } finally {
                    sendParts[0] = null;
                }
                try (Spot.SpotMessages ignored =
                       BenchUtil.spotRecvMessagesWithTimeout(spotSub, 5000)) {
                }
            }
            double latUs = (System.nanoTime() - t0) / 1000.0 / latCount;

            final int[] recvCount = {0};
            final Spot fSpotSub = spotSub;
            final int fMsgCount = msgCount;
            Thread recvThread = new Thread(() -> {
                for (int i = 0; i < fMsgCount; i++) {
                    try {
                        try (Spot.SpotMessages ignored =
                               BenchUtil.spotRecvMessagesWithTimeout(fSpotSub, 5000)) {
                        }
                        recvCount[0]++;
                    } catch (Exception e) {
                        break;
                    }
                }
            });

            recvThread.start();
            int sent = 0;
            t0 = System.nanoTime();
            for (int i = 0; i < msgCount; i++) {
                try {
                    try (Message msg = Message.fromNativeData(payloadSegment)) {
                        sendParts[0] = msg;
                        spotPub.publishMove("bench", sendParts, SendFlag.NONE);
                    } finally {
                        sendParts[0] = null;
                    }
                    sent++;
                } catch (Exception e) {
                    break;
                }
            }
            recvThread.join();

            double sec = (System.nanoTime() - t0) / 1_000_000_000.0;
            double thr = (sent > 0 && recvCount[0] > 0) ? (Math.min(sent, recvCount[0]) / sec) : 0.0;
            BenchUtil.printResult("SPOT", transport, size, thr, latUs);
            return 0;
        } catch (Exception e) {
            return 2;
        } finally {
            try {
                if (spotSub != null) {
                    spotSub.close();
                }
            } catch (Exception ignored) {
            }
            try {
                if (spotPub != null) {
                    spotPub.close();
                }
            } catch (Exception ignored) {
            }
            try {
                if (nodeSub != null) {
                    nodeSub.close();
                }
            } catch (Exception ignored) {
            }
            try {
                if (nodePub != null) {
                    nodePub.close();
                }
            } catch (Exception ignored) {
            }
            try {
                if (payloadArena != null) {
                    payloadArena.close();
                }
            } catch (Exception ignored) {
            }
            try {
                ctx.close();
            } catch (Exception ignored) {
            }
        }
    }
}
