using TicTacToe.Shared.Contracts;

namespace TicTacToe.Server.Configuration;

internal sealed record SampleSettings(
    string InstanceName,
    int ApiIndex,
    int PlayIndex,
    string ApiBindUrl,
    string ApiPublicUrl,
    string ApiChannelEndpoint,
    IReadOnlyList<string> ApiChannelEndpoints,
    string PlayChannelEndpoint,
    IReadOnlyList<string> PlayChannelEndpoints,
    string PlayEndpoint,
    IReadOnlyList<string> PlayEndpoints,
    string SpotEndpoint,
    IReadOnlyList<string> SpotEndpoints,
    string SpotPubSubEndpoint,
    IReadOnlyList<string> SpotPubSubEndpoints,
    string PlaySpotNodeRid,
    string PeerPlaySpotNodeRid,
    string PeerSpotEndpoint,
    string PeerSpotPubEndpoint,
    string RedisEndpoint,
    string RedisKeyPrefix,
    string LogDirectory)
{
    public IReadOnlyList<PlayNodeInfo> PlayNodes =>
        PlayEndpoints
            .Select((endpoint, index) => new PlayNodeInfo(endpoint, PlaySpotNodeRidAt(index)))
            .ToArray();

    public static SampleSettings Load(string[] args, string? mode = null)
    {
        var defaults = CreateDefault(mode);
        if (args.Length != 2 || args[0] != "--config" || string.IsNullOrWhiteSpace(args[1]))
            throw new ArgumentException("Usage: --config PATH");

        var configPath = args[1];
        var builder = new ConfigurationBuilder();
        builder.AddJsonFile(configPath, false);

        var section = builder.Build().GetSection("Sample");
        var apiIndex = ReadInt(section[nameof(ApiIndex)], defaults.ApiIndex);
        var playIndex = ReadInt(section[nameof(PlayIndex)], defaults.PlayIndex);
        var apiBindUrls = ReadList(section, "ApiBindUrls", defaults.ApiBindUrl);
        var apiPublicUrls = ReadList(section, "ApiPublicUrls", defaults.ApiPublicUrl);
        var apiChannelEndpoints = ReadList(section, nameof(ApiChannelEndpoints), defaults.ApiChannelEndpoint);
        var playChannelEndpoints = ReadList(section, nameof(PlayChannelEndpoints), defaults.PlayChannelEndpoint);
        var playEndpoints = ReadList(section, nameof(PlayEndpoints), defaults.PlayEndpoint);
        var spotEndpoints = ReadList(section, nameof(SpotEndpoints), defaults.SpotEndpoint);
        var spotPubSubEndpoints = ReadList(section, nameof(SpotPubSubEndpoints), defaults.SpotPubSubEndpoint);
        var peerPlayIndex = playIndex == 0 ? 1 : 0;
        var resolved = new SampleSettings(
            section[nameof(InstanceName)] ?? defaults.InstanceName,
            apiIndex,
            playIndex,
            section[nameof(ApiBindUrl)] ?? At(apiBindUrls, apiIndex),
            section[nameof(ApiPublicUrl)] ?? At(apiPublicUrls, apiIndex),
            section[nameof(ApiChannelEndpoint)] ?? At(apiChannelEndpoints, apiIndex),
            apiChannelEndpoints,
            section[nameof(PlayChannelEndpoint)] ?? At(playChannelEndpoints, playIndex),
            playChannelEndpoints,
            section[nameof(PlayEndpoint)] ?? At(playEndpoints, playIndex),
            playEndpoints,
            section[nameof(SpotEndpoint)] ?? At(spotEndpoints, playIndex),
            spotEndpoints,
            section[nameof(SpotPubSubEndpoint)] ?? At(spotPubSubEndpoints, playIndex),
            spotPubSubEndpoints,
            section[nameof(PlaySpotNodeRid)] ?? PlaySpotNodeRidAt(playIndex),
            section[nameof(PeerPlaySpotNodeRid)] ?? PlaySpotNodeRidAt(peerPlayIndex),
            section[nameof(PeerSpotEndpoint)] ?? At(spotEndpoints, peerPlayIndex),
            section[nameof(PeerSpotPubEndpoint)] ?? At(spotPubSubEndpoints, peerPlayIndex),
            section[nameof(RedisEndpoint)] ?? defaults.RedisEndpoint,
            section[nameof(RedisKeyPrefix)] ?? defaults.RedisKeyPrefix,
            section[nameof(LogDirectory)] ?? defaults.LogDirectory);

        // The sample owns its Redis via run_sample.sh/run_sample.ps1, which provisions
        // an isolated container; require the endpoint so a stray direct run never
        // silently falls back to a developer's local Redis.
        if (string.IsNullOrWhiteSpace(resolved.RedisEndpoint))
            throw new InvalidOperationException(
                "RedisEndpoint is required; run the sample via run_sample.sh/run_sample.ps1, "
                + "which provisions an isolated Redis container.");

        return resolved;
    }

    private static SampleSettings CreateDefault(string? mode)
    {
        var (instanceName, apiIndex, playIndex) = ResolveMode(mode);
        var apiBindUrls = new[] { "http://127.0.0.1:18080", "http://127.0.0.1:18085" };
        var apiChannelEndpoints = new[] { "tcp://127.0.0.1:18081", "tcp://127.0.0.1:18086" };
        var playChannelEndpoints = new[] { "tcp://127.0.0.1:18082", "tcp://127.0.0.1:18087" };
        var playEndpoints = new[] { "tcp://127.0.0.1:18084", "tcp://127.0.0.1:18089" };
        var spotEndpoints = new[] { "tcp://127.0.0.1:18090", "tcp://127.0.0.1:18091" };
        var spotPubSubEndpoints = new[] { "tcp://127.0.0.1:18092", "tcp://127.0.0.1:18093" };
        var peerPlayIndex = playIndex == 0 ? 1 : 0;
        return new SampleSettings(
            instanceName,
            apiIndex,
            playIndex,
            At(apiBindUrls, apiIndex),
            At(apiBindUrls, apiIndex),
            At(apiChannelEndpoints, apiIndex),
            apiChannelEndpoints,
            At(playChannelEndpoints, playIndex),
            playChannelEndpoints,
            At(playEndpoints, playIndex),
            playEndpoints,
            At(spotEndpoints, playIndex),
            spotEndpoints,
            At(spotPubSubEndpoints, playIndex),
            spotPubSubEndpoints,
            PlaySpotNodeRidAt(playIndex),
            PlaySpotNodeRidAt(peerPlayIndex),
            At(spotEndpoints, peerPlayIndex),
            At(spotPubSubEndpoints, peerPlayIndex),
            string.Empty,
            "tictactoe:",
            Path.Combine("logs", "tictactoe"));
    }

    private static (string InstanceName, int ApiIndex, int PlayIndex) ResolveMode(string? mode)
    {
        return mode switch
        {
            "api-b" => ("api-b", 1, 0),
            "play-b" => ("play-b", 0, 1),
            "api-a" or "api" => ("api-a", 0, 0),
            "play-a" or "play" => ("play-a", 0, 0),
            _ => ("sample", 0, 0)
        };
    }

    private static IReadOnlyList<string> ReadList(
        IConfigurationSection section,
        string name,
        string fallback)
    {
        var values = section.GetSection(name)
            .GetChildren()
            .Select(static child => child.Value)
            .Where(static value => !string.IsNullOrWhiteSpace(value))
            .Select(static value => value!)
            .ToArray();
        return values.Length == 0 ? new[] { fallback } : values;
    }

    private static string At(IReadOnlyList<string> values, int index)
    {
        if (values.Count == 0) throw new ArgumentException("Endpoint list must not be empty.", nameof(values));

        return values[Math.Clamp(index, 0, values.Count - 1)];
    }

    private static int ReadInt(string? value, int fallback)
    {
        return int.TryParse(value, out var parsed) ? parsed : fallback;
    }

    private static string PlaySpotNodeRidAt(int index)
    {
        return $"play-node-{index + 1}";
    }

}
