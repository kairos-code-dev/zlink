package dev.kairoscode.zlink.integration.bench;

import dev.kairoscode.zlink.*;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

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
        Arena ioArena = null;

        try {
            String endpoint = BenchUtil.endpointFor(transport, "stream");
            server.setSockOpt(SocketOption.SNDTIMEO, ioTimeoutMs);
            server.setSockOpt(SocketOption.RCVTIMEO, ioTimeoutMs);
            client.setSockOpt(SocketOption.SNDTIMEO, ioTimeoutMs);
            client.setSockOpt(SocketOption.RCVTIMEO, ioTimeoutMs);
            server.bind(endpoint);
            client.connect(endpoint);
            BenchUtil.sleep(300);
            ioArena = Arena.ofShared();

            byte[] serverClientId = BenchUtil.streamExpectConnectEvent(server);
            byte[] clientServerId = BenchUtil.streamExpectConnectEvent(client);
            MemorySegment serverClientIdSeg = ioArena.allocate(serverClientId.length);
            MemorySegment.copy(MemorySegment.ofArray(serverClientId), 0,
              serverClientIdSeg, 0, serverClientId.length);
            MemorySegment clientServerIdSeg = ioArena.allocate(clientServerId.length);
            MemorySegment.copy(MemorySegment.ofArray(clientServerId), 0,
              clientServerIdSeg, 0, clientServerId.length);

            byte[] buf = new byte[size];
            for (int i = 0; i < size; i++) {
                buf[i] = 'a';
            }
            MemorySegment payloadSeg = ioArena.allocate(size);
            MemorySegment.copy(MemorySegment.ofArray(buf), 0, payloadSeg, 0, size);

            int cap = Math.max(256, size);
            MemorySegment recvRidSeg = ioArena.allocate(256);
            MemorySegment recvPayloadSeg = ioArena.allocate(cap);

            for (int i = 0; i < warmup; i++) {
                BenchUtil.streamSendConst(client, clientServerIdSeg,
                  clientServerId.length,
                  payloadSeg, size);
                BenchUtil.streamRecv(server, recvRidSeg, 256, recvPayloadSeg, cap);
            }

            long t0 = System.nanoTime();
            for (int i = 0; i < latCount; i++) {
                BenchUtil.streamSendConst(client, clientServerIdSeg,
                  clientServerId.length,
                  payloadSeg, size);
                int rxLen = BenchUtil.streamRecv(server, recvRidSeg, 256,
                  recvPayloadSeg, cap);
                BenchUtil.streamSendConst(server, serverClientIdSeg,
                  serverClientId.length,
                  recvPayloadSeg, rxLen);
                BenchUtil.streamRecv(client, recvRidSeg, 256, recvPayloadSeg, cap);
            }
            double latUs = (System.nanoTime() - t0) / 1000.0 / (latCount * 2.0);

            final int[] recvCount = {0};
            final Socket fServer = server;
            Thread receiver = new Thread(() -> {
                try (Arena threadArena = Arena.ofConfined()) {
                    MemorySegment threadRidSeg = threadArena.allocate(256);
                    MemorySegment threadPayloadSeg = threadArena.allocate(cap);
                    for (int i = 0; i < msgCount; i++) {
                        try {
                            BenchUtil.streamRecv(fServer, threadRidSeg, 256,
                              threadPayloadSeg, cap);
                        } catch (Exception e) {
                            break;
                        }
                        recvCount[0]++;
                    }
                }
            });

            receiver.start();
            int sent = 0;
            t0 = System.nanoTime();
            for (int i = 0; i < msgCount; i++) {
                try {
                    BenchUtil.streamSendConst(client, clientServerIdSeg,
                      clientServerId.length, payloadSeg, size);
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
                if (ioArena != null) {
                    ioArena.close();
                }
            } catch (Exception ignored) {
            }
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
