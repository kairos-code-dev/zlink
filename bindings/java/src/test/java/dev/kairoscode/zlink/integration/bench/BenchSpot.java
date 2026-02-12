package dev.kairoscode.zlink.integration.bench;

import dev.kairoscode.zlink.*;

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

            for (int i = 0; i < warmup; i++) {
                spotPub.publish("bench", new Message[]{Message.fromBytes(payload)}, SendFlag.NONE);
                BenchUtil.spotRecvWithTimeout(spotSub, 5000);
            }

            long t0 = System.nanoTime();
            for (int i = 0; i < latCount; i++) {
                spotPub.publish("bench", new Message[]{Message.fromBytes(payload)}, SendFlag.NONE);
                BenchUtil.spotRecvWithTimeout(spotSub, 5000);
            }
            double latUs = (System.nanoTime() - t0) / 1000.0 / latCount;

            final int[] recvCount = {0};
            final Spot fSpotSub = spotSub;
            final int fMsgCount = msgCount;
            Thread recvThread = new Thread(() -> {
                for (int i = 0; i < fMsgCount; i++) {
                    try {
                        BenchUtil.spotRecvWithTimeout(fSpotSub, 5000);
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
                    spotPub.publish("bench", new Message[]{Message.fromBytes(payload)}, SendFlag.NONE);
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
                ctx.close();
            } catch (Exception ignored) {
            }
        }
    }
}
