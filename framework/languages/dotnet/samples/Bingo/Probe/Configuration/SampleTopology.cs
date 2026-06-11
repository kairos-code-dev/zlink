namespace Bingo.Probe.Configuration;

public sealed record SampleTopology(
    string ApiChannelEndpoint,
    string PlayChannelEndpoint)
{
    public static SampleTopology Create()
    {
        return new SampleTopology(
            ReadEndpoint("BINGO_API_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47103"),
            ReadEndpoint("BINGO_PLAY_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47104"));
    }

    private static string ReadEndpoint(string name, string defaultValue)
    {
        var value = Environment.GetEnvironmentVariable(name);
        return string.IsNullOrWhiteSpace(value) ? defaultValue : value;
    }
}
