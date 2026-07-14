namespace Bingo.Server.Configuration;

// Keeps the message-flow stream separate so it is greppable by correlation id.
public static class SampleFlowLog
{
    public static string Path(string logDirectory, string role)
    {
        return System.IO.Path.Combine(logDirectory, $"flow-{role}.log");
    }
}
