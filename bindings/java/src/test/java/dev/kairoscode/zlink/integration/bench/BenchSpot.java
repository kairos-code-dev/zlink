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
        // Spot default uses const single-part path; override via env.
        int globalConst = BenchUtil.parseEnv("BENCH_USE_CONST", 1);
        boolean useConst = BenchUtil.parseEnv("BENCH_SPOT_USE_CONST",
          globalConst) == 1;
        int maxSpot = BenchUtil.parseEnv("BENCH_SPOT_MSG_COUNT_MAX", 50000);
        if (msgCount > maxSpot) {
            msgCount = maxSpot;
        }

        Context ctx = new Context();
        SpotNode nodePub = null;
        SpotNode nodeSub = null;
        Spot spotPub = null;
        Spot spotSub = null;
        Spot.PreparedTopic preparedTopic = null;
        Spot.PublishContext publishContext = null;
        Spot.RecvContext recvContext = null;
        Arena payloadArena = null;
        try {
            nodePub = new SpotNode(ctx);
            nodeSub = new SpotNode(ctx);
            String endpoint = BenchUtil.endpointFor(transport, "spot");
            nodePub.bind(endpoint);
            nodeSub.connectPeerPub(endpoint);
            spotPub = new Spot(nodePub);
            spotSub = new Spot(nodeSub);
            preparedTopic = spotPub.prepareTopic("bench");
            publishContext = spotPub.createPublishContext();
            spotSub.subscribe(preparedTopic);
            recvContext = spotSub.createRecvContext();
            BenchUtil.sleep(300);

            byte[] payload = new byte[size];
            for (int i = 0; i < size; i++) {
                payload[i] = 'a';
            }
            payloadArena = Arena.ofShared();
            MemorySegment payloadSegment = payloadArena.allocate(size);
            MemorySegment.copy(MemorySegment.ofArray(payload), 0, payloadSegment, 0, size);
            for (int i = 0; i < warmup; i++) {
                if (useConst) {
                    spotPub.publishConst(preparedTopic, payloadSegment,
                      SendFlag.NONE, publishContext);
                } else {
                    publishMove(spotPub, preparedTopic, payloadSegment,
                      publishContext);
                }
                BenchUtil.spotRecvRawBlocking(spotSub, recvContext);
            }

            long t0 = System.nanoTime();
            for (int i = 0; i < latCount; i++) {
                if (useConst) {
                    spotPub.publishConst(preparedTopic, payloadSegment,
                      SendFlag.NONE, publishContext);
                } else {
                    publishMove(spotPub, preparedTopic, payloadSegment,
                      publishContext);
                }
                BenchUtil.spotRecvRawBlocking(spotSub, recvContext);
            }
            double latUs = (System.nanoTime() - t0) / 1000.0 / latCount;

            final int[] recvCount = {0};
            final Spot fSpotSub = spotSub;
            final int fMsgCount = msgCount;
            Thread recvThread = new Thread(() -> {
                try (Spot.RecvContext threadRecvContext = fSpotSub.createRecvContext()) {
                    for (int i = 0; i < fMsgCount; i++) {
                        try {
                            BenchUtil.spotRecvRawWithTimeout(
                              fSpotSub, threadRecvContext, 5000);
                            recvCount[0]++;
                        } catch (Exception e) {
                            break;
                        }
                    }
                }
            });

            recvThread.start();
            int sent = 0;
            t0 = System.nanoTime();
            for (int i = 0; i < msgCount; i++) {
                try {
                    if (useConst) {
                        spotPub.publishConst(preparedTopic, payloadSegment,
                          SendFlag.NONE, publishContext);
                    } else {
                        publishMove(spotPub, preparedTopic, payloadSegment,
                          publishContext);
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
                if (publishContext != null) {
                    publishContext.close();
                }
            } catch (Exception ignored) {
            }
            try {
                if (recvContext != null) {
                    recvContext.close();
                }
            } catch (Exception ignored) {
            }
            try {
                if (preparedTopic != null) {
                    preparedTopic.close();
                }
            } catch (Exception ignored) {
            }
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

    private static void publishMove(Spot spotPub,
                                    Spot.PreparedTopic preparedTopic,
                                    MemorySegment payloadSegment,
                                    Spot.PublishContext publishContext) {
        try (Message msg = Message.fromNativeData(payloadSegment)) {
            spotPub.publishMove(preparedTopic, msg, SendFlag.NONE,
              publishContext);
        }
    }
}
