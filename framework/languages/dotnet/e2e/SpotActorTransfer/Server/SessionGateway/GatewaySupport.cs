using System.Collections.Concurrent;
using SpotActorTransfer.Shared;

namespace SpotActorTransfer.SessionGateway;

internal sealed record GatewayOptions(
    string Rid,
    string HttpUrl,
    string RedisEndpoint,
    string RedisKeyPrefix,
    string RouterEndpoint,
    string StreamEndpoint,
    string EvidenceFile)
{
    public static GatewayOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var i = 0; i < args.Length; i += 2)
        {
            if (i + 1 >= args.Length) throw new ArgumentException($"Missing value for '{args[i]}'.");
            values[args[i].TrimStart('-')] = args[i + 1];
        }
        string Required(string key) => values.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"--{key} is required.");
        return new GatewayOptions(
            Required("rid"), Required("http-url"), Required("redis-endpoint"),
            Required("redis-key-prefix"), Required("router-endpoint"),
            Required("stream-endpoint"), Required("evidence-file"));
    }
}

internal sealed class GatewayEvidenceStore(string nodeRid, string path)
{
    private readonly ConcurrentQueue<ActorEvidence> _items = new();

    public void Add(string scenario, string actorId, string kind, string value)
    {
        var item = new ActorEvidence(scenario, actorId, kind, value, nodeRid);
        _items.Enqueue(item);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.AppendAllLines(path, [$"{item.Scenario}|{item.ActorId}|{item.Kind}|{item.Value}|{item.NodeRid}"]);
    }
}
