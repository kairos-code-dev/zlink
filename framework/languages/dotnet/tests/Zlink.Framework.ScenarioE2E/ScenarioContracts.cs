using System.Runtime.CompilerServices;

namespace Zlink.Framework.ScenarioE2E;

// Packet contracts shared by the scenario server and the per-scenario clients.
// These mirror what a real application would put in a Shared contracts project.
internal sealed record ScenarioProfileRequest(string RequestId, string Value);

internal sealed record ScenarioProfileReply(string RequestId, string Value, string ServerId);

internal sealed record ScenarioProfileCommand(string CommandId, string Value);

internal sealed record ScenarioRoutePingRequest(string RequestId, string Value);

internal sealed record ScenarioRoutePingReply(string RequestId, string Value, string TargetRid, string SourceRid);

// The shared server exposes this snapshot over its /evidence HTTP endpoint so a
// client scenario can confirm what the server actually dispatched.
internal sealed record ScenarioEvidenceSnapshot(
    string ServerId,
    string[] RequestIds,
    string[] CompletedRequestIds,
    string[] CommandIds,
    string[] RouteRequestIds,
    string[] RouteSourceRids,
    string[] DispatchErrors);

// Connection facts the runner hands to each scenario. This is plain data, not a
// helper around the framework: every scenario still builds its own host and calls
// the public client API directly.
internal sealed record ScenarioContext(
    string Channel,
    IReadOnlyList<string> ServerNames,
    IReadOnlyList<string> ZLinkEndpoints,
    IReadOnlyList<string> HttpEndpoints,
    string ClientRoutingId,
    string ClientZLinkEndpoint,
    IReadOnlyList<string> RoutePeerEndpoints);

internal static class Scenario
{
    // The single assertion primitive the scenarios use, just like the samples.
    // CallerArgumentExpression turns the failing condition into the message.
    public static void Ensure(
        bool condition,
        [CallerArgumentExpression(nameof(condition))] string? expression = null)
    {
        if (!condition)
        {
            throw new InvalidOperationException($"Ensure failed: {expression}");
        }
    }
}
