namespace Bingo.Server.Configuration;

// Resolves the per-role message-flow tracing log path. Honors $BINGO_LOG_DIR
// (default "logs"); kept separate from the app log so the flow stream is greppable
// by correlation id on its own.
public static class SampleFlowLog
{
    public static string Path(string role)
    {
        var dir = System.Environment.GetEnvironmentVariable("BINGO_LOG_DIR") ?? "logs";
        return System.IO.Path.Combine(dir, $"flow-{role}.log");
    }
}
