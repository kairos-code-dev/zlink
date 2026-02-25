using System;
using System.Threading;
using Zlink;

internal static partial class PerfRunner
{
    internal static int RunSpot(string transport, int size)
    {
        int warmupSeconds = ParseEnv("PERF_SINGLE_WARMUP_SECONDS", 3);
        int settleMs = ParseEnv("PERF_SINGLE_SETTLE_MS", 300);
        int durationSeconds = ParseEnv("PERF_SINGLE_DURATION_SECONDS", 5);
        int drainMs = ParseEnv("PERF_SINGLE_DRAIN_MS", 300);
        int latCount = ParseEnv("PERF_LAT_COUNT", 200);

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
            var recvPayload = new byte[Math.Max(256, size)];

            var warmupDeadline = DateTime.UtcNow.AddSeconds(Math.Max(0,
                warmupSeconds));
            while (DateTime.UtcNow < warmupDeadline)
            {
                spotPub.Publish("bench", payload.AsSpan(), SendFlags.None);
                SpotReceivePayloadWithTimeout(spotSub, recvPayload.AsSpan(), 5000);
            }

            Thread.Sleep(Math.Max(0, settleMs));

            int recvCount = 0;
            var sw = System.Diagnostics.Stopwatch.StartNew();
            var throughputDeadline = DateTime.UtcNow.AddSeconds(
                Math.Max(1, durationSeconds));
            while (DateTime.UtcNow < throughputDeadline)
            {
                spotPub.Publish("bench", payload.AsSpan(), SendFlags.None);
                SpotReceivePayloadWithTimeout(spotSub, recvPayload.AsSpan(), 5000);
                recvCount++;
            }
            sw.Stop();
            double thr = recvCount > 0 && sw.Elapsed.TotalSeconds > 0.0
                ? recvCount / sw.Elapsed.TotalSeconds
                : 0.0;

            Thread.Sleep(Math.Max(0, drainMs));

            int latRecv = 0;
            sw.Restart();
            for (int i = 0; i < latCount; i++)
            {
                spotPub.Publish("bench", payload.AsSpan(), SendFlags.None);
                SpotReceivePayloadWithTimeout(spotSub, recvPayload.AsSpan(), 5000);
                latRecv++;
            }
            sw.Stop();
            double latUs = (sw.Elapsed.TotalMilliseconds * 1000.0)
                / Math.Max(1, latRecv);
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
