using ResilienceLifecycle.Server.Registry.Configuration;
using ResilienceLifecycle.Server.Registry.Handlers;
using ResilienceLifecycle.Server.Registry.Infrastructure;
using ResilienceLifecycle.Server.Registry;
namespace ResilienceLifecycle.Server.Registry.Endpoints;

internal sealed record TopologyEntryResult(string? RoutingId, string Endpoint, string State);
