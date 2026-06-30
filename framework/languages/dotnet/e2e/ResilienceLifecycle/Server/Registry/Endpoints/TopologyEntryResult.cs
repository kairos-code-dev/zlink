namespace ResilienceLifecycle.Server.Registry.Endpoints;

internal sealed record TopologyEntryResult(string? RoutingId, string Endpoint, string State);