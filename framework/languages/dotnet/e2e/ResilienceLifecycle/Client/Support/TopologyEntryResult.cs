namespace ResilienceLifecycle.Client;

internal sealed record TopologyEntryResult(string? RoutingId, string Endpoint, string State);
