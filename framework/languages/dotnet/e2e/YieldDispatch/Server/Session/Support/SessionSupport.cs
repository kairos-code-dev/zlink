using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using YieldDispatch.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Actors;

namespace YieldDispatch.Server.Session;

internal sealed record NodeOptions(string Rid);

internal sealed class EvidenceStore
{
    private readonly object _gate = new();
    private readonly List<string> _entries = [];
    private readonly string? _filePath;

    public EvidenceStore(string rid, string? filePath)
    {
        Rid = rid;
        _filePath = filePath;
    }

    public string Rid { get; }

    public void Add(string entry)
    {
        lock (_gate)
        {
            _entries.Add(entry);
            if (!string.IsNullOrWhiteSpace(_filePath))
            {
                File.AppendAllText(_filePath, entry + Environment.NewLine);
            }
        }
    }

    public string[] Snapshot()
    {
        lock (_gate)
        {
            return _entries.ToArray();
        }
    }
}

internal sealed record SessionOptions(
    string Rid,
    string HttpUrl,
    string RegistryRouterEndpoint,
    string ControlEndpoint,
    string PlayControlEndpoint,
    string SpotRouterEndpoint,
    string StreamEndpoint,
    string LogDir)
{
    public string EvidenceFile => Path.Combine(LogDir, $"{Rid}.evidence.log");

    public static SessionOptions Parse(string[] args)
    {
        var values = Cli.Parse(args);
        return new SessionOptions(
            Cli.Required(values, "rid"),
            Cli.Required(values, "http-url"),
            Cli.Required(values, "registry-router-endpoint"),
            Cli.Required(values, "control-endpoint"),
            Cli.Required(values, "play-control-endpoint"),
            Cli.Required(values, "spot-router-endpoint"),
            Cli.Required(values, "stream-endpoint"),
            Cli.Required(values, "log-dir"));
    }
}

internal static class Cli
{
    public static Dictionary<string, string> Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i < args.Length; i++)
        {
            if (!args[i].StartsWith("--", StringComparison.Ordinal))
            {
                continue;
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for {args[i]}.");
            }

            values[args[i][2..]] = args[++i];
        }

        return values;
    }

    public static string Required(Dictionary<string, string> values, string key)
        => values.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"--{key} is required.");
}
