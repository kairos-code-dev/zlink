using System;
using System.Diagnostics;

internal static partial class PerfRunner
{
    internal static int RunMultiClient(string pattern, string transport, int size,
        string endpoint)
    {
        string normalizedPattern = pattern.ToUpperInvariant();
        string outputPattern = NormalizePerfPattern(normalizedPattern);
        size = Math.Max(1, size);

        if (!ParseEndpointArg(endpoint, out string normalizedEndpoint))
            return 1;

        Environment.SetEnvironmentVariable("PERF_PATTERN", outputPattern);

        var process = Process.GetCurrentProcess();
        TimeSpan cpuStart = process.TotalProcessorTime;
        var wall = Stopwatch.StartNew();

        try
        {
            int rc = outputPattern switch
            {
                "DEALER_DEALER" =>
                    PerfDealerDealerClient.Run(transport, size,
                        normalizedEndpoint),
                "DEALER_ROUTER" =>
                    PerfDealerRouterClient.Run(transport, size,
                        normalizedEndpoint),
                "ROUTER_ROUTER" =>
                    PerfRouterRouterClient.Run(transport, size,
                        normalizedEndpoint),
                "PUBSUB" =>
                    PerfPubSubClient.Run(transport, size, normalizedEndpoint),
                "SPOT" =>
                    PerfSpotClient.Run(transport, size, normalizedEndpoint),
                "STREAM" => PrintStreamClientUnsupported(outputPattern,
                    transport, size),
                _ => 1,
            };

            if (rc != 0)
                return rc;

            wall.Stop();
            EmitClientProcessMetrics(process, cpuStart, wall.Elapsed,
                outputPattern, transport, size);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"multi_client_error:{ex.GetType().Name}:{ex.Message}\n{ex}");
            return 2;
        }
    }

    private static int PrintStreamClientUnsupported(string pattern,
        string transport, int size)
    {
        Console.WriteLine($"UNSUPPORTED,{pattern},{transport},{size},stream_client_external");
        return 0;
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
