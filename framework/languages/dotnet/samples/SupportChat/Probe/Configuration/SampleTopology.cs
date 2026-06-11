namespace SupportChat.Probe.Configuration;

public sealed record SampleTopology(
    string ApiChannelEndpoint,
    string SupportChannelEndpoint)
{
    public static SampleTopology Create()
    {
        return new SampleTopology(
            ReadEndpoint("SUPPORTCHAT_API_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47203"),
            ReadEndpoint("SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47204"));
    }

    private static string ReadEndpoint(string name, string defaultValue)
    {
        var value = Environment.GetEnvironmentVariable(name);
        return string.IsNullOrWhiteSpace(value) ? defaultValue : value;
    }
}
