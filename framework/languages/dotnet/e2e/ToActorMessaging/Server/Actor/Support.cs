using System.Collections.Concurrent;
using ToActorMessaging.Shared;

namespace ToActorMessaging.Actor;

internal sealed record ServerOptions(
    string Rid,
    string HttpUrl,
    string RedisEndpoint,
    string RedisKeyPrefix,
    string RouterEndpoint,
    string PubSubEndpoint,
    string EvidenceFile,
    string LogDir)
{
    public static ServerOptions Parse(string[] args, string role)
    {
        var values = ParseArgs(args);
        var logDir = Get(values, "log-dir", Path.Combine(AppContext.BaseDirectory, "logs"));
        return new ServerOptions(
            Get(values, "rid", role),
            Get(values, "http-url", "http://127.0.0.1:0"),
            Get(values, "redis-endpoint", "127.0.0.1:6379"),
            Get(values, "redis-key-prefix", "zlink:e2e:to-actor"),
            Get(values, "router-endpoint", "tcp://127.0.0.1:0"),
            Get(values, "pubsub-endpoint", "tcp://127.0.0.1:0"),
            Get(values, "evidence-file", Path.Combine(logDir, $"{role}.evidence.log")),
            logDir);
    }

    private static Dictionary<string, string> ParseArgs(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var i = 0; i < args.Length; i += 2)
        {
            var key = args[i].TrimStart('-');
            if (i + 1 >= args.Length) throw new ArgumentException($"Missing value for '{args[i]}'.");
            values[key] = args[i + 1];
        }

        return values;
    }

    private static string Get(Dictionary<string, string> values, string key, string fallback) =>
        values.TryGetValue(key, out var value) ? value : fallback;
}

internal sealed class EvidenceStore(string path)
{
    private readonly ConcurrentQueue<ActorEvidence> _items = new();

    public void Append(ActorEvidence evidence)
    {
        _items.Enqueue(evidence);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.AppendAllLines(path,
        [
            $"{evidence.Scenario}|{evidence.ActorId}|{evidence.Kind}|{evidence.Value}"
            + $"|node={evidence.NodeRid ?? "<none>"}"
            + $"|generation={evidence.Generation?.ToString() ?? "<none>"}"
            + $"|packet={evidence.PacketName ?? "<none>"}"
            + $"|request={evidence.RequestId ?? "<none>"}"
        ]);
    }

    public IReadOnlyList<ActorEvidence> All() => _items.ToArray();
}
