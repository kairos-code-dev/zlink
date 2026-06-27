using ResilienceLifecycle.Server.Registry.Configuration;
using ResilienceLifecycle.Server.Registry.Endpoints;
using ResilienceLifecycle.Server.Registry.Handlers;
using ResilienceLifecycle.Server.Registry;
namespace ResilienceLifecycle.Server.Registry.Infrastructure;

internal sealed class FaultState
{
    public string Mode { get; set; } = "none";
}
