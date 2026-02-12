using System;
using System.Threading;
using Zlink;

internal static partial class BenchRunner
{
    internal static int RunSpot(string transport, int size)
    {
        int warmup = ParseEnv("BENCH_WARMUP_COUNT", 200);
        int latCount = ParseEnv("BENCH_LAT_COUNT", 200);
        int msgCount = Math.Min(ResolveMsgCount(size), ParseEnv("BENCH_SPOT_MSG_COUNT_MAX", 50000));

        using var ctx = new Context();
        SpotNode? nodePub = null;
        SpotNode? nodeSub = null;
        Spot? spotPub = null;
        Spot? spotSub = null;
        try
        {
            nodePub = new SpotNode(ctx);
            nodeSub = new SpotNode(ctx);
            string endpoint = EndpointFor(transport, "spot");
            nodePub.Bind(endpoint);
            nodeSub.ConnectPeerPub(endpoint);
            spotPub = new Spot(nodePub);
            spotSub = new Spot(nodeSub);
            spotSub.Subscribe("bench");
            Thread.Sleep(300);

            var payload = new byte[size];
            Array.Fill(payload, (byte)'a');
            using var payloadMessage = Message.FromBytes(payload.AsSpan());
            var payloadParts = new[] { payloadMessage };

            for (int i = 0; i < warmup; i++)
            {
                spotPub.Publish("bench", payloadParts, SendFlags.None);
                SpotReceiveWithTimeout(spotSub, 5000);
            }

            var sw = System.Diagnostics.Stopwatch.StartNew();
            for (int i = 0; i < latCount; i++)
            {
                spotPub.Publish("bench", payloadParts, SendFlags.None);
                SpotReceiveWithTimeout(spotSub, 5000);
            }
            sw.Stop();
            double latUs = (sw.Elapsed.TotalMilliseconds * 1000.0) / latCount;

            int recvCount = 0;
            var recvThread = new Thread(() =>
            {
                for (int i = 0; i < msgCount; i++)
                {
                    try
                    {
                        SpotReceiveWithTimeout(spotSub, 5000);
                        recvCount++;
                    }
                    catch
                    {
                        break;
                    }
                }
            });

            recvThread.Start();
            int sent = 0;
            sw.Restart();
            for (int i = 0; i < msgCount; i++)
            {
                try
                {
                    spotPub.Publish("bench", payloadParts, SendFlags.None);
                }
                catch
                {
                    break;
                }
                sent++;
            }
            recvThread.Join();
            sw.Stop();

            double thr = (sent > 0 && recvCount > 0)
              ? Math.Min(sent, recvCount) / sw.Elapsed.TotalSeconds
              : 0.0;
            PrintResult("SPOT", transport, size, thr, latUs);
            return 0;
        }
        catch
        {
            return 2;
        }
        finally
        {
            try { spotSub?.Dispose(); } catch { }
            try { spotPub?.Dispose(); } catch { }
            try { nodeSub?.Dispose(); } catch { }
            try { nodePub?.Dispose(); } catch { }
        }
    }
}
