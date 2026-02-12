using System;
using System.Threading;
using Zlink;

internal static partial class BenchRunner
{
    internal static int RunStream(string transport, int size)
    {
        int warmup = ParseEnv("BENCH_WARMUP_COUNT", 1000);
        int latCount = ParseEnv("BENCH_LAT_COUNT", 500);
        int msgCount = ResolveMsgCount(size);
        int ioTimeoutMs = ParseEnv("BENCH_STREAM_TIMEOUT_MS", 5000);

        using var ctx = new Context();
        using var server = new Zlink.Socket(ctx, SocketType.Stream);
        using var client = new Zlink.Socket(ctx, SocketType.Stream);

        try
        {
            string ep = EndpointFor(transport, "stream");
            server.SetOption(SocketOption.SndTimeo, ioTimeoutMs);
            server.SetOption(SocketOption.RcvTimeo, ioTimeoutMs);
            client.SetOption(SocketOption.SndTimeo, ioTimeoutMs);
            client.SetOption(SocketOption.RcvTimeo, ioTimeoutMs);
            server.Bind(ep);
            client.Connect(ep);
            Thread.Sleep(300);

            var serverClientId = new byte[256];
            int serverClientIdLen = StreamExpectConnectEvent(server,
                serverClientId.AsSpan());
            var clientServerId = new byte[256];
            int clientServerIdLen = StreamExpectConnectEvent(client,
                clientServerId.AsSpan());

            var buf = new byte[size];
            Array.Fill(buf, (byte)'a');
            int cap = Math.Max(256, size);
            var recvId = new byte[256];
            var recvPayload = new byte[cap];
            var ackId = new byte[256];
            var ackPayload = new byte[cap];

            for (int i = 0; i < warmup; i++)
            {
                StreamSend(client, clientServerId.AsSpan(0, clientServerIdLen),
                    buf.AsSpan());
                StreamRecv(server, recvId.AsSpan(), recvPayload.AsSpan(),
                    out _, out _);
            }

            var sw = System.Diagnostics.Stopwatch.StartNew();
            for (int i = 0; i < latCount; i++)
            {
                StreamSend(client, clientServerId.AsSpan(0, clientServerIdLen),
                    buf.AsSpan());
                StreamRecv(server, recvId.AsSpan(), recvPayload.AsSpan(),
                    out _, out int payloadLen);
                StreamSend(server, serverClientId.AsSpan(0, serverClientIdLen),
                    recvPayload.AsSpan(0, payloadLen));
                StreamRecv(client, ackId.AsSpan(), ackPayload.AsSpan(), out _,
                    out _);
            }
            sw.Stop();
            double latUs = (sw.Elapsed.TotalMilliseconds * 1000.0) / (latCount * 2);

            int recvCount = 0;
            var recvThread = new Thread(() =>
            {
                var thrId = new byte[256];
                var thrPayload = new byte[cap];
                for (int i = 0; i < msgCount; i++)
                {
                    try
                    {
                        StreamRecv(server, thrId.AsSpan(), thrPayload.AsSpan(),
                            out _, out _);
                    }
                    catch
                    {
                        break;
                    }
                    recvCount++;
                }
            });

            recvThread.Start();
            int sent = 0;
            sw.Restart();
            for (int i = 0; i < msgCount; i++)
            {
                try
                {
                    StreamSend(client, clientServerId.AsSpan(0, clientServerIdLen),
                        buf.AsSpan());
                }
                catch
                {
                    break;
                }
                sent++;
            }
            recvThread.Join();
            sw.Stop();

            int effective = Math.Min(sent, recvCount);
            double thr = (effective > 0) ? (effective / sw.Elapsed.TotalSeconds) : 0.0;
            PrintResult("STREAM", transport, size, thr, latUs);
            return 0;
        }
        catch
        {
            return 2;
        }
    }
}
