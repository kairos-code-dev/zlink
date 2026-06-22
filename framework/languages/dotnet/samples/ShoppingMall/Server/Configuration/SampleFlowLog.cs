namespace ShoppingMall.Server.Configuration;

public static class SampleFlowLog
{
    public static string Path(string role)
    {
        var dir = Environment.GetEnvironmentVariable("SHOPPINGMALL_LOG_DIR") ?? "logs";
        return System.IO.Path.Combine(dir, $"flow-{role}.log");
    }
}
