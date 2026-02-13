package dev.kairoscode.zlink.integration.bench;

import dev.kairoscode.zlink.*;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

final class BenchGateway {
    private BenchGateway() {
    }

    static int run(String transport, int size) {
        int warmup = BenchUtil.parseEnv("BENCH_WARMUP_COUNT", 1000);
        int latCount = BenchUtil.parseEnv("BENCH_LAT_COUNT", 500);
        int msgCount = BenchUtil.resolveMsgCount(size);
        // Gateway default uses const single-part path; override via env.
        int globalConst = BenchUtil.parseEnvFlag("BENCH_USE_CONST", 1);
        boolean useConst = BenchUtil.parseEnvFlag("BENCH_GATEWAY_USE_CONST",
          globalConst) == 1;

        Context ctx = new Context();
        Registry registry = null;
        Discovery discovery = null;
        Receiver receiver = null;
        Socket router = null;
        Gateway gateway = null;
        Gateway.PreparedService preparedService = null;
        Gateway.SendContext sendContext = null;
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
            sendContext = gateway.createSendContext();
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
            MemorySegment ridSegment = payloadArena.allocate(256);
            MemorySegment payloadRecvSegment = payloadArena.allocate(dataCap);
            for (int i = 0; i < warmup; i++) {
                if (useConst) {
                    gateway.sendConst(preparedService, payloadSegment,
                      SendFlag.NONE, sendContext);
                } else {
                    gatewaySendMove(gateway, preparedService, payloadSegment,
                      sendContext);
                }
                recvGatewayPayloadBlocking(router, ridSegment,
                  payloadRecvSegment, dataCap);
            }

            long t0 = System.nanoTime();
            for (int i = 0; i < latCount; i++) {
                if (useConst) {
                    gateway.sendConst(preparedService, payloadSegment,
                      SendFlag.NONE, sendContext);
                } else {
                    gatewaySendMove(gateway, preparedService, payloadSegment,
                      sendContext);
                }
                recvGatewayPayloadBlocking(router, ridSegment,
                  payloadRecvSegment, dataCap);
            }
            double latUs = (System.nanoTime() - t0) / 1000.0 / latCount;

            final int[] recvCount = {0};
            final Socket fRouter = router;
            Thread recvThread = new Thread(() -> {
                for (int i = 0; i < msgCount; i++) {
                    try {
                        recvGatewayPayloadWithTimeout(fRouter, ridSegment,
                          payloadRecvSegment, dataCap, 5000);
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
                    if (useConst) {
                        gateway.sendConst(preparedService, payloadSegment,
                          SendFlag.NONE, sendContext);
                    } else {
                        gatewaySendMove(gateway, preparedService, payloadSegment,
                          sendContext);
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
                if (sendContext != null) {
                    sendContext.close();
                }
            } catch (Exception ignored) {
            }
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

    private static void gatewaySendMove(Gateway gateway,
                                        Gateway.PreparedService service,
                                        MemorySegment payload,
                                        Gateway.SendContext sendContext) {
        try (Message msg = Message.fromNativeData(payload)) {
            gateway.sendMove(service, msg, SendFlag.NONE, sendContext);
        }
    }

    private static void recvGatewayPayloadBlocking(Socket router,
                                                   MemorySegment ridBuffer,
                                                   MemorySegment payloadBuffer,
                                                   int dataCap) {
        BenchUtil.recvBlocking(router, ridBuffer, 256);
        for (int i = 0; i < 3; i++) {
            int payloadLen = BenchUtil.recvBlocking(router, payloadBuffer,
              dataCap);
            if (payloadLen > 0)
                return;
        }
        throw new RuntimeException("gateway payload frame is empty");
    }

    private static void recvGatewayPayloadWithTimeout(Socket router,
                                                      MemorySegment ridBuffer,
                                                      MemorySegment payloadBuffer,
                                                      int dataCap,
                                                      int timeoutMs) {
        BenchUtil.recvWithTimeout(router, ridBuffer, 256, timeoutMs);
        for (int i = 0; i < 3; i++) {
            int payloadLen = BenchUtil.recvWithTimeout(router, payloadBuffer,
              dataCap, timeoutMs);
            if (payloadLen > 0)
                return;
        }
        throw new RuntimeException("gateway payload frame is empty");
    }
}
