package dev.kairoscode.zlink.integration.bench;

import dev.kairoscode.zlink.*;

import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicBoolean;

final class BenchDealerRouter {
    private BenchDealerRouter() {
    }

    static int run(String transport, int size) {
        int warmup = BenchUtil.parseEnv("BENCH_WARMUP_COUNT", 1000);
        int latCount = BenchUtil.parseEnv("BENCH_LAT_COUNT", 1000);
        int msgCount = BenchUtil.resolveMsgCount(size);

        Context ctx = new Context();
        Socket router = new Socket(ctx, SocketType.ROUTER);
        Socket dealer = new Socket(ctx, SocketType.DEALER);

        try {
            String endpoint = BenchUtil.endpointFor(transport, "dealer-router");
            dealer.setSockOpt(SocketOption.ROUTING_ID, "CLIENT".getBytes(StandardCharsets.UTF_8));
            router.bind(endpoint);
            dealer.connect(endpoint);
            BenchUtil.sleep(300);

            byte[] buf = new byte[size];
            for (int i = 0; i < size; i++) {
                buf[i] = 'a';
            }

            for (int i = 0; i < warmup; i++) {
                dealer.send(buf, SendFlag.NONE);
                byte[] rid = router.recv(256, ReceiveFlag.NONE);
                router.recv(size, ReceiveFlag.NONE);
                router.send(rid, SendFlag.SNDMORE);
                router.send(buf, SendFlag.NONE);
                dealer.recv(size, ReceiveFlag.NONE);
            }

            long t0 = System.nanoTime();
            for (int i = 0; i < latCount; i++) {
                dealer.send(buf, SendFlag.NONE);
                byte[] rid = router.recv(256, ReceiveFlag.NONE);
                router.recv(size, ReceiveFlag.NONE);
                router.send(rid, SendFlag.SNDMORE);
                router.send(buf, SendFlag.NONE);
                dealer.recv(size, ReceiveFlag.NONE);
            }
            double latUs = (System.nanoTime() - t0) / 1000.0 / (latCount * 2.0);

            AtomicBoolean recvDone = new AtomicBoolean(false);
            AtomicBoolean recvFail = new AtomicBoolean(false);
            Thread receiver = new Thread(() -> {
                try {
                    for (int i = 0; i < msgCount; i++) {
                        router.recv(256, ReceiveFlag.NONE);
                        router.recv(size, ReceiveFlag.NONE);
                    }
                    recvDone.set(true);
                } catch (Exception e) {
                    recvFail.set(true);
                }
            });

            receiver.start();
            t0 = System.nanoTime();
            for (int i = 0; i < msgCount; i++) {
                dealer.send(buf, SendFlag.NONE);
            }
            receiver.join();

            if (!recvDone.get() || recvFail.get()) {
                return 2;
            }

            double sec = (System.nanoTime() - t0) / 1_000_000_000.0;
            double thr = msgCount / sec;
            BenchUtil.printResult("DEALER_ROUTER", transport, size, thr, latUs);
            return 0;
        } catch (Exception e) {
            return 2;
        } finally {
            try {
                router.close();
            } catch (Exception ignored) {
            }
            try {
                dealer.close();
            } catch (Exception ignored) {
            }
            try {
                ctx.close();
            } catch (Exception ignored) {
            }
        }
    }
}
