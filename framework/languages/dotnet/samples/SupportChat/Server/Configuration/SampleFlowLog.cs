namespace SupportChat.Server.Configuration;

public static class SampleFlowLog
{
    public static string Path(string role)
    {
        var dir = Environment.GetEnvironmentVariable("SUPPORTCHAT_LOG_DIR") ?? "logs";
        return System.IO.Path.Combine(dir, $"flow-{role}.log");
    }
}
