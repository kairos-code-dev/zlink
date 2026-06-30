namespace ResilienceLifecycle.Server.Registry.Endpoints;

internal sealed record TopologyEntryRes(string? RoutingId, string Endpoint, string State);