using RegistryMessaging.Server.Provider.Endpoints;
using RegistryMessaging.Server.Provider.Handlers;
using RegistryMessaging.Server.Provider.Infrastructure;
using RegistryMessaging.Server.Provider;
namespace RegistryMessaging.Server.Provider.Configuration;

internal sealed record ServerOptions(
    string Role,
    string HttpUrl,
    string LogDir,
    string? EvidenceFile,
    string Rid,
    string? RegistryPubEndpoint,
    string? RegistryRouterEndpoint,
    string? ChannelEndpoint,
    string? ManualClientEndpoint,
    string? WorkflowEndpoint,
    string? RouteEndpoint,
    int Weight,
    IReadOnlyList<string> RoutePeers)
{
    public static ServerOptions Parse(string[] args, string defaultRole)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var routePeers = new List<string>();

        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
            {
                continue;
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for {key}.");
            }

            var value = args[++i];
            if (key == "--route-peer")
            {
                routePeers.Add(value);
            }
            else
            {
                values[key[2..]] = value;
            }
        }

        var rid = values.GetValueOrDefault("rid", "node");
        Environment.SetEnvironmentVariable("ZLINK_E2E_RID", rid);
        return new ServerOptions(
            defaultRole,
            values.GetValueOrDefault("http-url", "http://127.0.0.1:0"),
            values.GetValueOrDefault("log-dir", Path.Combine(Path.GetTempPath(), "zlink-dotnet-e2e-log")),
            values.GetValueOrDefault("evidence-file"),
            rid,
            values.GetValueOrDefault("registry-pub-endpoint"),
            values.GetValueOrDefault("registry-router-endpoint"),
            values.GetValueOrDefault("channel-endpoint"),
            values.GetValueOrDefault("manual-client-endpoint"),
            values.GetValueOrDefault("workflow-endpoint"),
            values.GetValueOrDefault("route-endpoint"),
            int.TryParse(values.GetValueOrDefault("weight"), out var weight) ? weight : 100,
            routePeers);
    }
}
