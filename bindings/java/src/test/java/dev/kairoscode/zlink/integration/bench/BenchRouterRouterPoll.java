package dev.kairoscode.zlink.integration.bench;

import dev.kairoscode.zlink.*;

import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicBoolean;

final class BenchRouterRouterPoll {
    private BenchRouterRouterPoll() {
    }

    static int run(String transport, int size) {
        int latCount = BenchUtil.parseEnv("BENCH_LAT_COUNT", 1000);
        int msgCount = BenchUtil.resolveMsgCount(size);

        Context ctx = new Context();
        Socket router1 = new Socket(ctx, SocketType.ROUTER);
        Socket router2 = new Socket(ctx, SocketType.ROUTER);

        try {
            String endpoint = BenchUtil.endpointFor(transport, "router-router-poll");
            router1.setSockOpt(SocketOption.ROUTING_ID, "ROUTER1".getBytes(StandardCharsets.UTF_8));
            router2.setSockOpt(SocketOption.ROUTING_ID, "ROUTER2".getBytes(StandardCharsets.UTF_8));
            router1.setSockOpt(SocketOption.ROUTER_MANDATORY, 1);
            router2.setSockOpt(SocketOption.ROUTER_MANDATORY, 1);
            router1.bind(endpoint);
            router2.connect(endpoint);
            BenchUtil.sleep(300);

            boolean connected = false;
            for (int i = 0; i < 100; i++) {
                try {
                    router2.send("ROUTER1".getBytes(StandardCharsets.UTF_8), SendFlag.SNDMORE);
                    router2.send("PING".getBytes(StandardCharsets.UTF_8), SendFlag.DONTWAIT);
                } catch (Exception e) {
                    Thread.sleep(10);
                    continue;
                }

                if (!BenchUtil.waitForInput(router1, 0)) {
                    Thread.sleep(10);
                    continue;
                }

                try {
                    router1.recv(256, ReceiveFlag.DONTWAIT);
                    router1.recv(16, ReceiveFlag.DONTWAIT);
                    connected = true;
                    break;
                } catch (Exception e) {
                    Thread.sleep(10);
                }
            }

            if (!connected) {
                return 2;
            }

            router1.send("ROUTER2".getBytes(StandardCharsets.UTF_8), SendFlag.SNDMORE);
            router1.send("PONG".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);
            if (!BenchUtil.waitForInput(router2, 2000)) {
                return 2;
            }
            router2.recv(256, ReceiveFlag.NONE);
            router2.recv(16, ReceiveFlag.NONE);

            byte[] buf = new byte[size];
            for (int i = 0; i < size; i++) {
                buf[i] = 'a';
            }

            long t0 = System.nanoTime();
            for (int i = 0; i < latCount; i++) {
                router2.send("ROUTER1".getBytes(StandardCharsets.UTF_8), SendFlag.SNDMORE);
                router2.send(buf, SendFlag.NONE);

                if (!BenchUtil.waitForInput(router1, 2000)) {
                    return 2;
                }
                byte[] rid = router1.recv(256, ReceiveFlag.NONE);
                router1.recv(size, ReceiveFlag.NONE);

                router1.send(rid, SendFlag.SNDMORE);
                router1.send(buf, SendFlag.NONE);

                if (!BenchUtil.waitForInput(router2, 2000)) {
                    return 2;
                }
                router2.recv(256, ReceiveFlag.NONE);
                router2.recv(size, ReceiveFlag.NONE);
            }
            double latUs = (System.nanoTime() - t0) / 1000.0 / (latCount * 2.0);

            AtomicBoolean recvDone = new AtomicBoolean(false);
            AtomicBoolean recvFail = new AtomicBoolean(false);
            Thread receiver = new Thread(() -> {
                try {
                    for (int i = 0; i < msgCount; i++) {
                        if (!BenchUtil.waitForInput(router1, 2000)) {
                            recvFail.set(true);
                            return;
                        }
                        router1.recv(256, ReceiveFlag.NONE);
                        router1.recv(size, ReceiveFlag.NONE);
                    }
                    recvDone.set(true);
                } catch (Exception e) {
                    recvFail.set(true);
                }
            });

            receiver.start();
            t0 = System.nanoTime();
            for (int i = 0; i < msgCount; i++) {
                router2.send("ROUTER1".getBytes(StandardCharsets.UTF_8), SendFlag.SNDMORE);
                router2.send(buf, SendFlag.NONE);
            }
            receiver.join();

            if (!recvDone.get() || recvFail.get()) {
                return 2;
            }

            double sec = (System.nanoTime() - t0) / 1_000_000_000.0;
            double thr = msgCount / sec;
            BenchUtil.printResult("ROUTER_ROUTER_POLL", transport, size, thr, latUs);
            return 0;
        } catch (Exception e) {
            return 2;
        } finally {
            try {
                router1.close();
            } catch (Exception ignored) {
            }
            try {
                router2.close();
            } catch (Exception ignored) {
            }
            try {
                ctx.close();
            } catch (Exception ignored) {
            }
        }
    }
}
