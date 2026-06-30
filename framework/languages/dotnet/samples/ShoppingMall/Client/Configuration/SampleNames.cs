namespace ShoppingMall.Client.Configuration;

public static class OrderStatuses
{
    public const string Created = "Created";
    public const string Confirmed = "Confirmed";
    public const string Failed = "Failed";
}

public static class SampleTimings
{
    public static readonly TimeSpan HttpTimeout = TimeSpan.FromSeconds(5);
    public static readonly TimeSpan WorkflowTimeout = TimeSpan.FromSeconds(8);
}

public sealed record SampleTopology(
    string ApiAHttpUrl,
    string ApiBHttpUrl)
{
    public static SampleTopology Create()
    {
        return new SampleTopology(
            Read("SHOPPINGMALL_API_A_HTTP_URL", "http://127.0.0.1:48203"),
            Read("SHOPPINGMALL_API_B_HTTP_URL", "http://127.0.0.1:48204"));
    }

    private static string Read(string name, string fallback)
    {
        var value = Environment.GetEnvironmentVariable(name);
        return string.IsNullOrWhiteSpace(value) ? fallback : value;
    }
}