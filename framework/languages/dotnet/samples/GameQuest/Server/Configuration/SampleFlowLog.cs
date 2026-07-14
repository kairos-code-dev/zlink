namespace GameQuest.Server.Configuration;

public static class SampleFlowLog
{
    public static string Path(string logDirectory, string role)
    {
        return System.IO.Path.Combine(logDirectory, $"flow-{role}.log");
    }
}
