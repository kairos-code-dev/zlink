namespace LocationMessaging.Server.Workflow.Configuration;

internal sealed record ServerOptions(
    string Role,
    string HttpUrl,
    string LogDir,
    string? EvidenceFile,
    string Rid,
    string? RedisEndpoint,
    string? RedisKeyPrefix,
    string WorkflowEndpoint,
    int Weight)
{
    public static ServerOptions Parse(string[] args, string defaultRole)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

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

            values[key[2..]] = args[++i];
        }

        var rid = values.GetValueOrDefault("rid", "workflow");
        Environment.SetEnvironmentVariable("ZLINK_E2E_RID", rid);
        return new ServerOptions(
            defaultRole,
            values.GetValueOrDefault("http-url", "http://127.0.0.1:0"),
            values.GetValueOrDefault("log-dir", Path.Combine(Path.GetTempPath(), "zlink-dotnet-e2e-log")),
            values.GetValueOrDefault("evidence-file"),
            rid,
            values.GetValueOrDefault("redis-endpoint"),
            values.GetValueOrDefault("redis-key-prefix"),
            values.TryGetValue("workflow-endpoint", out var workflowEndpoint)
                && !string.IsNullOrWhiteSpace(workflowEndpoint)
                ? workflowEndpoint
                : throw new ArgumentException("--workflow-endpoint is required."),
            int.TryParse(values.GetValueOrDefault("weight"), out var weight) ? weight : 100);
    }
}
