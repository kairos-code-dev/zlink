using System;
using System.Diagnostics;

internal static partial class PerfRunner
{
    internal static int RunMultiClient(string pattern, string transport, int size,
        string endpoint)
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
                "MULTI_DEALER_DEALER" =>
                    RunMultiDealerDealerClient(transport, size, endpoint),
                "MULTI_DEALER_ROUTER" =>
                    RunMultiDealerRouterClient(transport, size, endpoint),
                "MULTI_ROUTER_ROUTER" =>
                    RunMultiRouterRouterClient(transport, size, endpoint),
                "MULTI_PUBSUB" => RunMultiPubSubClient(transport, size, endpoint),
                "MULTI_GATEWAY" => RunMultiGatewayClient(transport, size, endpoint),
                "MULTI_SPOT" => RunMultiSpotClient(transport, size, endpoint),
                "MULTI_STREAM" => RunMultiStreamClient(transport, size, endpoint),
                "MULTI_STREAM_CALLBACK" =>
                    RunMultiStreamCallbackClient(transport, size, endpoint),
                "MULTI_STREAM_LEN32BE" =>
                    RunMultiStreamLen32BeClient(transport, size, endpoint),
                _ => 1,
            };

            if (rc != 0)
                return rc;

            wall.Stop();
            EmitClientProcessMetrics(process, cpuStart, wall.Elapsed,
                normalizedPattern, transport, size);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"multi_client_error:{ex.Message}");
            return 2;
        }
    }

    private static void EmitClientProcessMetrics(Process process,
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
            $"RESULT,current,{pattern},{transport},{size},client_cpu_pct,{cpuPct}");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},client_mem_mb,{memMb}");
    }
}
