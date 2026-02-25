package dev.kairoscode.zlink.integration.bench;

import dev.kairoscode.zlink.*;

import java.util.concurrent.atomic.AtomicBoolean;

final class PerfDealerDealer {
    private PerfDealerDealer() {
    }

    static int run(String transport, int size) {
        int warmup = PerfUtil.parseEnv("PERF_WARMUP_COUNT", 1000);
        int latCount = PerfUtil.parseEnv("PERF_LAT_COUNT", 500);
        int msgCount = PerfUtil.resolveMsgCount(size);

        Context ctx = new Context();
        Socket a = new Socket(ctx, SocketType.DEALER);
        Socket b = new Socket(ctx, SocketType.DEALER);

        try {
            String endpoint = PerfUtil.endpointFor(transport, "dealer_dealer");
            a.bind(endpoint);
            b.connect(endpoint);
            PerfUtil.sleep(300);

            byte[] buf = new byte[size];
            for (int i = 0; i < size; i++) {
                buf[i] = 'a';
            }

            for (int i = 0; i < warmup; i++) {
                b.send(buf, SendFlag.NONE);
                a.recv(size, ReceiveFlag.NONE);
            }

            long t0 = System.nanoTime();
            for (int i = 0; i < latCount; i++) {
                b.send(buf, SendFlag.NONE);
                byte[] x = a.recv(size, ReceiveFlag.NONE);
                a.send(x, SendFlag.NONE);
                b.recv(size, ReceiveFlag.NONE);
            }
            double latUs = (System.nanoTime() - t0) / 1000.0 / (latCount * 2.0);

            AtomicBoolean recvDone = new AtomicBoolean(false);
            AtomicBoolean recvFail = new AtomicBoolean(false);
            Thread receiver = new Thread(() -> {
                try {
                    for (int i = 0; i < msgCount; i++) {
                        a.recv(size, ReceiveFlag.NONE);
                    }
                    recvDone.set(true);
                } catch (Exception e) {
                    recvFail.set(true);
                }
            });

            receiver.start();
            t0 = System.nanoTime();
            for (int i = 0; i < msgCount; i++) {
                b.send(buf, SendFlag.NONE);
            }
            receiver.join();

            if (!recvDone.get() || recvFail.get()) {
                return 2;
            }

            double sec = (System.nanoTime() - t0) / 1_000_000_000.0;
            double thr = msgCount / sec;
            PerfUtil.printResult("DEALER_DEALER", transport, size, thr, latUs);
            return 0;
        } catch (Exception e) {
            return 2;
        } finally {
            try {
                a.close();
            } catch (Exception ignored) {
            }
            try {
                b.close();
            } catch (Exception ignored) {
            }
            try {
                ctx.close();
            } catch (Exception ignored) {
            }
        }
    }
}
