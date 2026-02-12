package dev.kairoscode.zlink.integration.bench;

import dev.kairoscode.zlink.*;

final class BenchStream {
    private BenchStream() {
    }

    static int run(String transport, int size) {
        int warmup = BenchUtil.parseEnv("BENCH_WARMUP_COUNT", 1000);
        int latCount = BenchUtil.parseEnv("BENCH_LAT_COUNT", 500);
        int msgCount = BenchUtil.resolveMsgCount(size);
        int ioTimeoutMs = BenchUtil.parseEnv("BENCH_STREAM_TIMEOUT_MS", 5000);

        Context ctx = new Context();
        Socket server = new Socket(ctx, SocketType.STREAM);
        Socket client = new Socket(ctx, SocketType.STREAM);

        try {
            String endpoint = BenchUtil.endpointFor(transport, "stream");
            server.setSockOpt(SocketOption.SNDTIMEO, ioTimeoutMs);
            server.setSockOpt(SocketOption.RCVTIMEO, ioTimeoutMs);
            client.setSockOpt(SocketOption.SNDTIMEO, ioTimeoutMs);
            client.setSockOpt(SocketOption.RCVTIMEO, ioTimeoutMs);
            server.bind(endpoint);
            client.connect(endpoint);
            BenchUtil.sleep(300);

            byte[] serverClientId = BenchUtil.streamExpectConnectEvent(server);
            byte[] clientServerId = BenchUtil.streamExpectConnectEvent(client);

            byte[] buf = new byte[size];
            for (int i = 0; i < size; i++) {
                buf[i] = 'a';
            }

            int cap = Math.max(256, size);

            for (int i = 0; i < warmup; i++) {
                BenchUtil.streamSend(client, clientServerId, buf);
                BenchUtil.streamRecv(server, cap);
            }

            long t0 = System.nanoTime();
            for (int i = 0; i < latCount; i++) {
                BenchUtil.streamSend(client, clientServerId, buf);
                BenchUtil.StreamFrame rx = BenchUtil.streamRecv(server, cap);
                BenchUtil.streamSend(server, serverClientId, rx.payload());
                BenchUtil.streamRecv(client, cap);
            }
            double latUs = (System.nanoTime() - t0) / 1000.0 / (latCount * 2.0);

            final int[] recvCount = {0};
            final Socket fServer = server;
            Thread receiver = new Thread(() -> {
                for (int i = 0; i < msgCount; i++) {
                    try {
                        BenchUtil.streamRecv(fServer, cap);
                    } catch (Exception e) {
                        break;
                    }
                    recvCount[0]++;
                }
            });

            receiver.start();
            int sent = 0;
            t0 = System.nanoTime();
            for (int i = 0; i < msgCount; i++) {
                try {
                    BenchUtil.streamSend(client, clientServerId, buf);
                } catch (Exception e) {
                    break;
                }
                sent++;
            }
            receiver.join();

            int effective = Math.min(sent, recvCount[0]);
            double sec = (System.nanoTime() - t0) / 1_000_000_000.0;
            double thr = (effective > 0) ? (effective / sec) : 0.0;
            BenchUtil.printResult("STREAM", transport, size, thr, latUs);
            return 0;
        } catch (Exception e) {
            return 2;
        } finally {
            try {
                server.close();
            } catch (Exception ignored) {
            }
            try {
                client.close();
            } catch (Exception ignored) {
            }
            try {
                ctx.close();
            } catch (Exception ignored) {
            }
        }
    }
}
