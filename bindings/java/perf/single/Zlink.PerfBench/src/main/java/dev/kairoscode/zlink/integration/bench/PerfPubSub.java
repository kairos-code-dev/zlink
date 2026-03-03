package dev.kairoscode.zlink.integration.bench;

import dev.kairoscode.zlink.*;

import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

final class PerfPubSub {
    private PerfPubSub() {
    }

    static int run(String transport, int size) {
        int warmup = PerfUtil.parseEnv("PERF_WARMUP_COUNT", 1000);
        int msgCount = PerfUtil.resolveMsgCount(size);
        int sndHwm = PerfUtil.parseEnv("PERF_SINGLE_SNDHWM",
          msgCount + warmup + 1024);
        int rcvHwm = PerfUtil.parseEnv("PERF_SINGLE_RCVHWM",
          msgCount + warmup + 1024);
        int sndTimeoMs = PerfUtil.parseEnv("PERF_SINGLE_SNDTIMEO_MS", 1000);
        int rcvTimeoMs = PerfUtil.parseEnv("PERF_SINGLE_RCVTIMEO_MS", 1000);

        Context ctx = new Context();
        Socket pub = new Socket(ctx, SocketType.PUB);
        Socket sub = new Socket(ctx, SocketType.SUB);

        try {
            String endpoint = PerfUtil.endpointFor(transport, "pubsub");
            pub.setSockOpt(SocketOption.SNDHWM, sndHwm);
            sub.setSockOpt(SocketOption.RCVHWM, rcvHwm);
            pub.setSockOpt(SocketOption.SNDTIMEO, sndTimeoMs);
            sub.setSockOpt(SocketOption.RCVTIMEO, rcvTimeoMs);
            sub.setSockOpt(SocketOption.SUBSCRIBE, new byte[0]);
            pub.bind(endpoint);
            sub.connect(endpoint);
            PerfUtil.sleep(300);

            byte[] buf = new byte[size];
            byte[] recvBuf = new byte[size];
            for (int i = 0; i < size; i++) {
                buf[i] = 'a';
            }

            for (int i = 0; i < warmup; i++) {
                pub.send(buf, SendFlag.NONE);
                sub.recv(recvBuf, ReceiveFlag.NONE);
            }

            AtomicBoolean senderDone = new AtomicBoolean(false);
            AtomicBoolean sendFail = new AtomicBoolean(false);
            AtomicBoolean recvFail = new AtomicBoolean(false);
            AtomicInteger recvCount = new AtomicInteger(0);
            AtomicLong lastRecvNs = new AtomicLong(System.nanoTime());
            Thread receiver = new Thread(() -> {
                try {
                    while (recvCount.get() < msgCount) {
                        try {
                            sub.recv(recvBuf, ReceiveFlag.NONE);
                            recvCount.incrementAndGet();
                            lastRecvNs.set(System.nanoTime());
                            continue;
                        } catch (Exception e) {
                            if (senderDone.get()) {
                                long idleMs = (System.nanoTime()
                                  - lastRecvNs.get()) / 1_000_000L;
                                if (idleMs > 500)
                                    break;
                            }
                            PerfUtil.sleep(1);
                        }
                    }
                } catch (Exception e) {
                    recvFail.set(true);
                }
            });

            receiver.start();
            long t0 = System.nanoTime();
            for (int i = 0; i < msgCount; i++) {
                try {
                    pub.send(buf, SendFlag.NONE);
                } catch (Exception e) {
                    sendFail.set(true);
                    break;
                }
            }
            senderDone.set(true);
            receiver.join();

            int received = recvCount.get();
            if (sendFail.get() || recvFail.get() || received <= 0) {
                return 2;
            }

            double sec = (System.nanoTime() - t0) / 1_000_000_000.0;
            if (sec <= 0.0) {
                return 2;
            }
            double thr = received / sec;
            double latUs = (sec * 1_000_000.0) / received;
            PerfUtil.printResult("PUBSUB", transport, size, thr, latUs);
            return 0;
        } catch (Exception e) {
            return 2;
        } finally {
            try {
                pub.close();
            } catch (Exception ignored) {
            }
            try {
                sub.close();
            } catch (Exception ignored) {
            }
            try {
                ctx.close();
            } catch (Exception ignored) {
            }
        }
    }
}
