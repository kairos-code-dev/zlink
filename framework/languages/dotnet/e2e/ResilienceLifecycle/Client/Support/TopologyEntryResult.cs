namespace ResilienceLifecycle.Client.Support;

internal sealed record TopologyEntryResult(string? RoutingId, string Endpoint, string State);