namespace Bingo.Probe.Configuration;

public sealed record SampleTopology(
    string ApiAChannelEndpoint,
    string ApiBChannelEndpoint,
    string ApiAPlayRouteEndpoint,
    string ApiBPlayRouteEndpoint,
    string PlayAChannelEndpoint,
    string PlayBChannelEndpoint,
    string SessionAPlayRouteEndpoint,
    string SessionBPlayRouteEndpoint)
{
    public static SampleTopology Create()
    {
        return new SampleTopology(
            ReadEndpoint("BINGO_API_A_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47103"),
            ReadEndpoint("BINGO_API_B_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47117"),
            ReadEndpoint("BINGO_API_A_PLAY_ROUTE_ENDPOINT", "tcp://127.0.0.1:47118"),
            ReadEndpoint("BINGO_API_B_PLAY_ROUTE_ENDPOINT", "tcp://127.0.0.1:47119"),
            ReadEndpoint("BINGO_PLAY_A_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47104"),
            ReadEndpoint("BINGO_PLAY_B_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47114"),
            ReadEndpoint("BINGO_SESSION_A_PLAY_ROUTE_ENDPOINT", "tcp://127.0.0.1:47120"),
            ReadEndpoint("BINGO_SESSION_B_PLAY_ROUTE_ENDPOINT", "tcp://127.0.0.1:47121"));
    }

    private static string ReadEndpoint(string name, string defaultValue)
    {
        var value = Environment.GetEnvironmentVariable(name);
        return string.IsNullOrWhiteSpace(value) ? defaultValue : value;
    }
}
