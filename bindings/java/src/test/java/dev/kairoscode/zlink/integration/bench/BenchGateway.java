package dev.kairoscode.zlink.integration.bench;

import dev.kairoscode.zlink.*;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

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
        Gateway.PreparedService preparedService = null;
        Arena payloadArena = null;
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
            if (!waitRegisterReady(receiver, service, 5000)) {
                return 2;
            }
            router = receiver.routerSocket();

            gateway = new Gateway(ctx, discovery);
            preparedService = gateway.prepareService(service);
            final Discovery fDiscovery = discovery;
            final Gateway fGateway = gateway;
            if (!BenchUtil.waitUntil(() -> fDiscovery.receiverCount(service) > 0, 10000)) {
                return 2;
            }
            final Gateway.PreparedService fPreparedService = preparedService;
            if (!BenchUtil.waitUntil(() -> fGateway.connectionCount(fPreparedService) > 0,
              10000)) {
                return 2;
            }
            BenchUtil.sleep(300);

            byte[] payload = new byte[size];
            for (int i = 0; i < size; i++) {
                payload[i] = 'a';
            }
            int dataCap = Math.max(256, size);
            payloadArena = Arena.ofShared();
            MemorySegment payloadSegment = payloadArena.allocate(size);
            MemorySegment.copy(MemorySegment.ofArray(payload), 0, payloadSegment, 0, size);
            Message[] sendParts = new Message[1];

            for (int i = 0; i < warmup; i++) {
                gatewaySendMove(gateway, preparedService, payloadSegment, sendParts);
                recvGatewayPayloadBlocking(router, dataCap);
            }

            long t0 = System.nanoTime();
            for (int i = 0; i < latCount; i++) {
                gatewaySendMove(gateway, preparedService, payloadSegment, sendParts);
                recvGatewayPayloadBlocking(router, dataCap);
            }
            double latUs = (System.nanoTime() - t0) / 1000.0 / latCount;

            final int[] recvCount = {0};
            final Socket fRouter = router;
            Thread recvThread = new Thread(() -> {
                for (int i = 0; i < msgCount; i++) {
                    try {
                        recvGatewayPayloadWithTimeout(fRouter, dataCap, 5000);
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
                        gateway.sendMove(preparedService, sendParts, SendFlag.NONE);
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
            BenchUtil.printResult("GATEWAY", transport, size, thr, latUs);
            return 0;
        } catch (Exception e) {
            System.err.println("BenchGateway failed: transport=" + transport +
              ", size=" + size + ", error=" + e.getMessage());
            return 2;
        } finally {
            try {
                if (preparedService != null) {
                    preparedService.close();
                }
            } catch (Exception ignored) {
            }
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

    private static void gatewaySendMove(Gateway gateway,
                                        Gateway.PreparedService service,
                                        MemorySegment payload,
                                        Message[] sendParts) {
        try (Message msg = Message.fromNativeData(payload)) {
            sendParts[0] = msg;
            gateway.sendMove(service, sendParts, SendFlag.NONE);
        } finally {
            sendParts[0] = null;
        }
    }

    private static boolean waitRegisterReady(Receiver receiver,
                                             String service,
                                             int timeoutMs) {
        long deadline = System.currentTimeMillis() + timeoutMs;
        int status = -1;
        while (System.currentTimeMillis() < deadline) {
            try {
                Receiver.ReceiverResult result = receiver.registerResult(service);
                status = result.status();
                if (status == 0)
                    return true;
            } catch (Exception ignored) {
            }
            BenchUtil.sleep(20);
        }
        return status == 0;
    }

    private static void recvGatewayPayloadBlocking(Socket router,
                                                   int dataCap) {
        BenchUtil.recvBlocking(router, 256);
        for (int i = 0; i < 3; i++) {
            byte[] payload = BenchUtil.recvBlocking(router, dataCap);
            if (payload.length > 0)
                return;
        }
        throw new RuntimeException("gateway payload frame is empty");
    }

    private static void recvGatewayPayloadWithTimeout(Socket router,
                                                      int dataCap,
                                                      int timeoutMs) {
        BenchUtil.recvWithTimeout(router, 256, timeoutMs);
        for (int i = 0; i < 3; i++) {
            byte[] payload = BenchUtil.recvWithTimeout(router, dataCap, timeoutMs);
            if (payload.length > 0)
                return;
        }
        throw new RuntimeException("gateway payload frame is empty");
    }
}
