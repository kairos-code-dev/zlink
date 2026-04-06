using System;
using System.Diagnostics;

internal static partial class PerfRunner
{
    internal static int RunMultiClient(string pattern, string transport, int size,
        string endpoint)
    {
        string outputPattern = NormalizePerfPattern(pattern);
        size = Math.Max(1, size);
        PerfOptions preset = PerfOptions.FromMultiPattern(outputPattern);

        if (!ParseEndpointArg(endpoint, out string normalizedEndpoint))
            return 1;

        var process = Process.GetCurrentProcess();
        TimeSpan cpuStart = process.TotalProcessorTime;
        var wall = Stopwatch.StartNew();

        try
        {
            var options = PerfOptions.CreateMulti(PerfExecutionKind.MultiClient,
                outputPattern, transport, size, normalizedEndpoint,
                preset.RecvMode);
            if (!MultiPerfPatternRegistry.TryGet(outputPattern,
                    out IPerfPattern perfPattern))
            {
                return 1;
            }

            int rc = perfPattern.RunMultiClient(options);
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
