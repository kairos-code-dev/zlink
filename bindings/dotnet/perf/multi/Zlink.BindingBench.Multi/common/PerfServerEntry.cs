using System;
using System.Diagnostics;

internal static partial class PerfRunner
{
    internal static int RunMultiServer(string pattern, string transport, int size)
    {
        string outputPattern = NormalizePerfPattern(pattern);
        size = Math.Max(1, size);
        PerfOptions preset = PerfOptions.FromMultiPattern(outputPattern);

        var process = Process.GetCurrentProcess();
        TimeSpan cpuStart = process.TotalProcessorTime;
        var wall = Stopwatch.StartNew();

        try
        {
            var options = PerfOptions.CreateMulti(PerfExecutionKind.MultiServer,
                outputPattern, transport, size, string.Empty, preset.RecvMode);
            if (!MultiPerfPatternRegistry.TryGet(outputPattern,
                    out IPerfPattern perfPattern))
            {
                return 1;
            }

            int rc = perfPattern.RunMultiServer(options);
            if (rc != 0)
                return rc;

            wall.Stop();
            EmitServerProcessMetrics(process, cpuStart, wall.Elapsed,
                outputPattern, transport, size);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"multi_server_error:{ex.GetType().Name}:{ex.Message}\n{ex}");
            return 2;
        }
    }

    internal static int PrintUnsupported(string pattern, string transport,
        int size, string reason)
    {
        return PerfShared.PrintUnsupported(pattern, transport, size, reason);
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
        if (IsMultiStreamPattern(pattern))
            return;
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},server_snd_pending_max,0");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},server_rcv_pending_max,0");
        Console.WriteLine(
            $"RESULT,current,{pattern},{transport},{size},server_rcv_pending_end,0");
    }
}
