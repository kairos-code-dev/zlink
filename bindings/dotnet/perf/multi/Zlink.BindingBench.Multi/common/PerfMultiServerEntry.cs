using System;
using System.Diagnostics;

internal static partial class PerfRunner
{
    internal static int RunMultiServer(string pattern, string transport, int size)
    {
        string normalizedPattern = pattern.ToUpperInvariant();
        size = Math.Max(1, size);

        var process = Process.GetCurrentProcess();
        TimeSpan cpuStart = process.TotalProcessorTime;
        var wall = Stopwatch.StartNew();

        try
        {
            int rc = normalizedPattern switch
            {
                "MULTI_DEALER_DEALER" => RunMultiDealerDealerServer(transport, size),
                "MULTI_DEALER_ROUTER" => RunMultiDealerRouterServer(transport, size),
                "MULTI_ROUTER_ROUTER" => RunMultiRouterRouterServer(transport, size),
                "MULTI_PUBSUB" => RunMultiPubSubServer(transport, size),
                "MULTI_GATEWAY" => RunMultiGatewayServer(transport, size),
                "MULTI_SPOT" => RunMultiSpotServer(transport, size),
                "MULTI_STREAM" => RunMultiStreamServer(transport, size),
                "MULTI_STREAM_CALLBACK" =>
                    RunMultiStreamCallbackServer(transport, size),
                "MULTI_STREAM_LEN32BE" =>
                    RunMultiStreamLen32BeServer(transport, size),
                _ => 1,
            };

            if (rc != 0)
                return rc;

            wall.Stop();
            EmitServerProcessMetrics(process, cpuStart, wall.Elapsed, normalizedPattern,
                transport, size);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"multi_server_error:{ex.Message}");
            return 2;
        }
    }

    private static void EmitServerProcessMetrics(Process process,
        TimeSpan cpuStart, TimeSpan wallElapsed, string pattern, string transport,
        int size)
    {
        process.Refresh();
        TimeSpan cpuEnd = process.TotalProcessorTime;
        double cpuSec = Math.Max(0.0, (cpuEnd - cpuStart).TotalSeconds);
        double wallSec = Math.Max(1e-9, wallElapsed.TotalSeconds);
        double ncpu = Math.Max(1, Environment.ProcessorCount);
        double cpuPct = (cpuSec / (wallSec * ncpu)) * 100.0;
        double memMb = process.WorkingSet64 / (1024.0 * 1024.0);

        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},server_cpu_pct,{cpuPct}");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},server_mem_mb,{memMb}");
    }
}
