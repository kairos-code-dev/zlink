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

    public static SampleSettings LoadApi(string[] args)
    {
        var section = LoadSection(args);
        var apiIndex = RequireIndex(section, nameof(ApiIndex));
        var playEndpoints = RequireList(section, nameof(PlayEndpoints), 2);
        return new SampleSettings(
            RequireString(section, nameof(InstanceName)),
            apiIndex,
            0,
            RequireString(section, nameof(ApiBindUrl)),
            string.Empty,
            RequireString(section, nameof(ApiChannelEndpoint)),
            [],
            string.Empty,
            RequireList(section, nameof(PlayChannelEndpoints), 2),
            string.Empty,
            playEndpoints,
            string.Empty,
            [],
            string.Empty,
            [],
            string.Empty,
            string.Empty,
            string.Empty,
            string.Empty,
            string.Empty,
            string.Empty,
            RequireString(section, nameof(LogDirectory)));
    }

    public static SampleSettings LoadPlay(string[] args)
    {
        var section = LoadSection(args);
        var playIndex = RequireIndex(section, nameof(PlayIndex));
        return new SampleSettings(
            RequireString(section, nameof(InstanceName)),
            0,
            playIndex,
            string.Empty,
            string.Empty,
            string.Empty,
            RequireList(section, nameof(ApiChannelEndpoints), 2),
            RequireString(section, nameof(PlayChannelEndpoint)),
            [],
            RequireString(section, nameof(PlayEndpoint)),
            RequireList(section, nameof(PlayEndpoints), 2),
            RequireString(section, nameof(SpotEndpoint)),
            [],
            RequireString(section, nameof(SpotPubSubEndpoint)),
            [],
            RequireString(section, nameof(PlaySpotNodeRid)),
            RequireString(section, nameof(PeerPlaySpotNodeRid)),
            RequireString(section, nameof(PeerSpotEndpoint)),
            RequireString(section, nameof(PeerSpotPubEndpoint)),
            RequireString(section, nameof(RedisEndpoint)),
            RequireString(section, nameof(RedisKeyPrefix)),
            RequireString(section, nameof(LogDirectory)));
    }

    private static IConfigurationSection LoadSection(string[] args)
    {
        if (args.Length != 2 || args[0] != "--config" || string.IsNullOrWhiteSpace(args[1]))
            throw new ArgumentException("Usage: --config PATH");

        return new ConfigurationBuilder()
            .AddJsonFile(Path.GetFullPath(args[1]), optional: false, reloadOnChange: false)
            .Build()
            .GetRequiredSection("Sample");
    }

    private static string RequireString(IConfigurationSection section, string name)
    {
        var value = section[name];
        return string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"Sample.{name} is required.")
            : value;
    }

    private static int RequireIndex(IConfigurationSection section, string name)
    {
        return int.TryParse(section[name], out var value) && value is 0 or 1
            ? value
            : throw new InvalidOperationException($"Sample.{name} must be 0 or 1.");
    }

    private static IReadOnlyList<string> RequireList(
        IConfigurationSection section,
        string name,
        int count)
    {
        var values = section.GetSection(name)
            .GetChildren()
            .Select(static child => child.Value)
            .Where(static value => !string.IsNullOrWhiteSpace(value))
            .Select(static value => value!)
            .ToArray();
        return values.Length == count
            ? values
            : throw new InvalidOperationException($"Sample.{name} must contain {count} values.");
    }

    private static string PlaySpotNodeRidAt(int index) => $"play-node-{index + 1}";

}
