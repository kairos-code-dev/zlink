using TicTacToe.Shared.Contracts;

namespace TicTacToe.Server.Configuration;

internal sealed record SampleSettings(
    string InstanceName,
    int PlayIndex,
    string ApiBindUrl,
    string MeshEndpoint,
    IReadOnlyList<string> PeerMeshEndpoints,
    string PlayEndpoint,
    IReadOnlyList<string> PlayEndpoints,
    string PlayMeshNodeRid,
    string RedisEndpoint,
    string RedisKeyPrefix,
    string LogDirectory)
{
    public IReadOnlyList<PlayNodeInfo> PlayNodes =>
        PlayEndpoints
            .Select((endpoint, index) => new PlayNodeInfo(endpoint, PlayMeshNodeRidAt(index)))
            .ToArray();

    public static SampleSettings LoadApi(string[] args)
    {
        var section = LoadSection(args);
        var playEndpoints = RequireList(section, nameof(PlayEndpoints), 2);
        return new SampleSettings(
            RequireString(section, nameof(InstanceName)),
            0,
            RequireString(section, nameof(ApiBindUrl)),
            RequireString(section, nameof(MeshEndpoint)),
            RequireList(section, nameof(PeerMeshEndpoints), 2),
            string.Empty,
            playEndpoints,
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
            playIndex,
            string.Empty,
            RequireString(section, nameof(MeshEndpoint)),
            ReadList(section, nameof(PeerMeshEndpoints)),
            RequireString(section, nameof(PlayEndpoint)),
            RequireList(section, nameof(PlayEndpoints), 2),
            RequireString(section, nameof(PlayMeshNodeRid)),
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
        var values = ReadList(section, name);
        return values.Count == count
            ? values
            : throw new InvalidOperationException($"Sample.{name} must contain {count} values.");
    }

    private static IReadOnlyList<string> ReadList(
        IConfigurationSection section,
        string name)
    {
        return section.GetSection(name)
            .GetChildren()
            .Select(static child => child.Value)
            .Where(static value => !string.IsNullOrWhiteSpace(value))
            .Select(static value => value!)
            .ToArray();
    }

    private static string PlayMeshNodeRidAt(int index) => $"play-node-{index + 1}";

}
