using System;

internal static partial class PerfRunner
{
    internal static int RunMultiServer(string pattern, string transport, int size)
    {
        string outputPattern = NormalizePerfPattern(pattern);
        size = Math.Max(1, size);
        PerfOptions preset = PerfOptions.FromMultiPattern(outputPattern);

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

            return 0;
        }
        catch (Exception ex)
        {
            if (PerfShared.TryPrintUnsupportedTransportFailure(outputPattern,
                    transport, size, ex))
            {
                return 0;
            }
            Console.Error.WriteLine($"multi_server_error:{ex.GetType().Name}:{ex.Message}\n{ex}");
            return 2;
        }
    }

    internal static int PrintUnsupported(string pattern, string transport,
        int size, string reason)
    {
        return PerfShared.PrintUnsupported(pattern, transport, size, reason);
    }
}
