package dev.kairoscode.zlink.integration.bench;

import dev.kairoscode.zlink.*;

final class BenchGateway {
    private BenchGateway() {
    }

    static int run(String transport, int size) {
        int warmup = BenchUtil.parseEnv("BENCH_WARMUP_COUNT", 200);
        int latCount = BenchUtil.parseEnv("BENCH_LAT_COUNT", 200);
        int msgCount = BenchUtil.resolveMsgCount(size);

        Context ctx = new Context();
        Registry registry = null;
        Discovery discovery = null;
        Receiver receiver = null;
        Socket router = null;
        Gateway gateway = null;
        try {
            String suffix = System.currentTimeMillis() + "-" + System.nanoTime();
            String regPub = "inproc://gw-pub-" + suffix;
            String regRouter = "inproc://gw-router-" + suffix;

            registry = new Registry(ctx);
            registry.setHeartbeat(5000, 60000);
            registry.setEndpoints(regPub, regRouter);
            registry.start();

            discovery = new Discovery(ctx, ServiceType.GATEWAY);
            discovery.connectRegistry(regPub);
            String service = "svc";
            discovery.subscribe(service);

            receiver = new Receiver(ctx);
            String providerEp = BenchUtil.endpointFor(transport, "gateway-provider");
            receiver.bind(providerEp);
            receiver.connectRegistry(regRouter);
            receiver.register(service, providerEp, 1);
            router = receiver.routerSocket();

            gateway = new Gateway(ctx, discovery);
            final Discovery fDiscovery = discovery;
            final Gateway fGateway = gateway;
            if (!BenchUtil.waitUntil(() -> fDiscovery.receiverCount(service) > 0, 5000)) {
                return 2;
            }
            if (!BenchUtil.waitUntil(() -> fGateway.connectionCount(service) > 0, 5000)) {
                return 2;
            }
            BenchUtil.sleep(300);

            byte[] payload = new byte[size];
            for (int i = 0; i < size; i++) {
                payload[i] = 'a';
            }
            int dataCap = Math.max(256, size);

            for (int i = 0; i < warmup; i++) {
                BenchUtil.gatewaySendWithRetry(gateway, service, payload, 5000);
                BenchUtil.recvWithTimeout(router, 256, 5000);
                BenchUtil.recvWithTimeout(router, dataCap, 5000);
            }

            long t0 = System.nanoTime();
            for (int i = 0; i < latCount; i++) {
                BenchUtil.gatewaySendWithRetry(gateway, service, payload, 5000);
                BenchUtil.recvWithTimeout(router, 256, 5000);
                BenchUtil.recvWithTimeout(router, dataCap, 5000);
            }
            double latUs = (System.nanoTime() - t0) / 1000.0 / latCount;

            final int[] recvCount = {0};
            final Socket fRouter = router;
            Thread recvThread = new Thread(() -> {
                for (int i = 0; i < msgCount; i++) {
                    try {
                        BenchUtil.recvWithTimeout(fRouter, 256, 5000);
                        BenchUtil.recvWithTimeout(fRouter, dataCap, 5000);
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
                    gateway.send(service, new Message[]{Message.fromBytes(payload)}, SendFlag.NONE);
                    sent++;
                } catch (Exception e) {
                    break;
                }
            }
            recvThread.join();

            double sec = (System.nanoTime() - t0) / 1_000_000_000.0;
            double thr = (sent > 0 && recvCount[0] > 0) ? (Math.min(sent, recvCount[0]) / sec) : 0.0;
            BenchUtil.printResult("GATEWAY", transport, size, thr, latUs);
            return 0;
        } catch (Exception e) {
            return 2;
        } finally {
            try {
                if (gateway != null) {
                    gateway.close();
                }
            } catch (Exception ignored) {
            }
            try {
                if (router != null) {
                    router.close();
                }
            } catch (Exception ignored) {
            }
            try {
                if (receiver != null) {
                    receiver.close();
                }
            } catch (Exception ignored) {
            }
            try {
                if (discovery != null) {
                    discovery.close();
                }
            } catch (Exception ignored) {
            }
            try {
                if (registry != null) {
                    registry.close();
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
