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

            var serverClientId = StreamExpectConnectEvent(server);
            var clientServerId = StreamExpectConnectEvent(client);

            var buf = new byte[size];
            Array.Fill(buf, (byte)'a');
            int cap = Math.Max(256, size);

            for (int i = 0; i < warmup; i++)
            {
                StreamSend(client, clientServerId, buf);
                StreamRecv(server, cap);
            }

            var sw = System.Diagnostics.Stopwatch.StartNew();
            for (int i = 0; i < latCount; i++)
            {
                StreamSend(client, clientServerId, buf);
                var rx = StreamRecv(server, cap);
                StreamSend(server, serverClientId, rx.Payload);
                StreamRecv(client, cap);
            }
            sw.Stop();
            double latUs = (sw.Elapsed.TotalMilliseconds * 1000.0) / (latCount * 2);

            int recvCount = 0;
            var recvThread = new Thread(() =>
            {
                for (int i = 0; i < msgCount; i++)
                {
                    try
                    {
                        StreamRecv(server, cap);
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
                    StreamSend(client, clientServerId, buf);
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
