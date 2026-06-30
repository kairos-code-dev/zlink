namespace ResilienceLifecycle.Client.Support;

internal sealed record TopologyEntryRes(string? RoutingId, string Endpoint, string State);