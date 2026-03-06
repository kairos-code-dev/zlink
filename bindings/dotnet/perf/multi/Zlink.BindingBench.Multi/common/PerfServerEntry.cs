using System;
using System.Diagnostics;

internal static partial class PerfRunner
{
    internal static int RunMultiServer(string pattern, string transport, int size)
    {
        string normalizedPattern = pattern.ToUpperInvariant();
        string outputPattern = NormalizePerfPattern(normalizedPattern);
        size = Math.Max(1, size);

        Environment.SetEnvironmentVariable("PERF_PATTERN", outputPattern);

        var process = Process.GetCurrentProcess();
        TimeSpan cpuStart = process.TotalProcessorTime;
        var wall = Stopwatch.StartNew();

        try
        {
            int rc = outputPattern switch
            {
                "DEALER_DEALER" => PerfDealerDealerServer.Run(transport, size),
                "DEALER_ROUTER" => PerfDealerRouterServer.Run(transport, size),
                "ROUTER_ROUTER" => PerfRouterRouterServer.Run(transport, size),
                "PUBSUB" => PerfPubSubServer.Run(transport, size),
                "GATEWAY" => PerfGatewayServer.Run(transport, size),
                "SPOT" => PerfSpotServer.Run(transport, size),
                "STREAM" => PerfStreamServer.Run(transport, size),
                "STREAM_CALLBACK" =>
                    PerfStreamCallbackServer.Run(transport, size),
                "STREAM_LEN32BE" =>
                    PerfStreamLen32BeServer.Run(transport, size),
                _ => 1,
            };

            if (rc != 0)
                return rc;

            wall.Stop();
            EmitServerProcessMetrics(process, cpuStart, wall.Elapsed,
                outputPattern, transport, size);
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
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},server_snd_pending_max,0");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},server_rcv_pending_max,0");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},server_rcv_pending_end,0");
    }
}
