namespace GameQuest.Server.Configuration;

public static class SampleFlowLog
{
    public static string Path(string role)
    {
        var dir = Environment.GetEnvironmentVariable("GAMEQUEST_LOG_DIR") ?? "logs";
        return System.IO.Path.Combine(dir, $"flow-{role}.log");
    }
}
