namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Owns the location lifecycle subdomains for this runtime and routes
/// ownership-loss events to the subdomain that tracks the affected row kind.
/// </summary>
internal sealed class ZLinkLocationLifecycle : IDisposable
{
    private readonly ZLinkLocationRuntime _runtime;

    internal ZLinkLocationLifecycle(
        ZLinkLocationRuntime runtime,
        ZLinkStoreLocationResolvers resolver)
    {
        _runtime = runtime;
        _runtime.OwnershipLost += OnOwnershipLost;
        ActorOwnership = new ZLinkActorOwnershipCoordinator(runtime, resolver);
        ActorSessionRoutes = new ZLinkActorSessionRouteLifecycle(runtime);
        SpotLocations = new ZLinkSpotLocationLifecycle(runtime, resolver);
    }

    internal ZLinkActorOwnershipCoordinator ActorOwnership { get; }

    internal ZLinkActorSessionRouteLifecycle ActorSessionRoutes { get; }

    internal ZLinkSpotLocationLifecycle SpotLocations { get; }

    public void Dispose()
    {
        _runtime.OwnershipLost -= OnOwnershipLost;
    }

    private void OnOwnershipLost(ZLinkLocationKind kind, string canonicalKey)
    {
        Func<CancellationToken, ValueTask>? deactivate = null;
        if (kind == ZLinkLocationKind.Actor)
        {
            deactivate = ActorOwnership.TakeOwnershipLostDeactivation(canonicalKey);
        }
        else if (kind == ZLinkLocationKind.Spot)
        {
            deactivate = SpotLocations.TakeOwnershipLostDeactivation(canonicalKey);
        }
        else if (kind == ZLinkLocationKind.Route)
        {
            ActorSessionRoutes.OnOwnershipLost(canonicalKey);
        }

        if (deactivate is not null)
        {
            _ = RunGuardedAsync(() => deactivate(CancellationToken.None));
        }
    }

    private static async Task RunGuardedAsync(Func<ValueTask> operation)
    {
        try
        {
            await operation().ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            ZLinkFrameworkDebugLog.SpotDiscovery($"location lifecycle error: {exception.Message}");
        }
    }
}
