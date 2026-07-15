namespace LocationMessaging.Server.Workflow.Configuration;

using Zlink.Framework.E2E.Configuration;

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
        => E2eConfiguration.Load<ServerOptions>(args) with { Role = defaultRole };
}
