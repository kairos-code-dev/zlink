namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorDirectory(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration,
    ZLinkStoreLocationResolvers? locations = null) : IZLinkActorDirectory
{
    private readonly ZLinkSpotMeshLocationResolver? _meshRows = locations is null
        ? null
        : new ZLinkSpotMeshLocationResolver(registration, locations);

    public async ValueTask<ActorRef?> FindAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        var (actorRef, _) = await FindWithPresenceAsync(actorId, cancellationToken)
            .ConfigureAwait(false);
        return actorRef;
    }

    /// <summary>Find plus the raw-row presence: a null ref with a present row
    /// is a transient resolve window (claimed-but-unpublished generation-0,
    /// lagging replica) worth retrying; a null ref without a row is a
    /// confirmed miss — the actor was destroyed or never existed.</summary>
    internal async ValueTask<(ActorRef? Ref, bool RowPresent)> FindWithPresenceAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(actorId);

        if (runtime.TryGetCreatedActorState(actorId, out var state)
            && state.NativeActorRef is { } localActorRef)
        {
            return (localActorRef.ToNative(), true);
        }

        if (locations is null)
        {
            return (null, false);
        }

        var (row, rowPresent) = await _meshRows!.ResolveActorWithPresenceAsync(actorId, cancellationToken)
            .ConfigureAwait(false);
        return (row?.ActorRef, rowPresent);
    }

    public async ValueTask<ActorRef> EnsureAsync(
        string actorId,
        ZLinkMessage createRequest,
        ZLinkActorPlacement placement = default,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(actorId);
        ArgumentNullException.ThrowIfNull(createRequest);

        if (await FindAsync(actorId, cancellationToken).ConfigureAwait(false) is { } existing)
        {
            return existing;
        }

        var actorType = ResolveSingleActorType();
        EnsurePlacementCanBeHostedHere(placement);

        try
        {
            var result = await runtime.CreateActorAsync(
                    actorId,
                    actorType,
                    createRequest,
                    cancellationToken)
                .ConfigureAwait(false);
            var state = runtime.GetOrCreateActorState(result.Actor.ActorId);
            var actorRef = state.NativeActorRef
                           ?? throw new ZLinkFrameworkException(
                               ZLinkFrameworkErrorKind.ActorRouteNotFound,
                               $"Actor '{actorId}' does not have a native Actor ref after creation.");
            return actorRef.ToNative();
        }
        catch (ZLinkFrameworkException ex)
            when (ex.Kind is ZLinkFrameworkErrorKind.ActorCreateFailed)
        {
            if (await FindAsync(actorId, cancellationToken).ConfigureAwait(false) is { } raced)
            {
                return raced;
            }

            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorCreateRejected,
                ex.Message,
                innerException: ex);
        }
    }

    private string ResolveSingleActorType()
    {
        var actorTypes = registration.SpotNodes.Values
            .SelectMany(static node => node.ActorFactories.Keys)
            .Distinct(StringComparer.Ordinal)
            .ToArray();

        return actorTypes.Length switch
        {
            1 => actorTypes[0],
            0 => throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorCreateFailed,
                "Actor directory requires one registered actor factory."),
            _ => throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorCreateFailed,
                "Actor directory cannot choose an actor type when multiple actor factories are registered.")
        };
    }

    private void EnsurePlacementCanBeHostedHere(ZLinkActorPlacement placement)
    {
        if (placement.PreferredNodeRid is not { } preferred || preferred.IsEmpty)
        {
            return;
        }

        var local = registration.SpotNodes.Values.Any(node => node.RoutingId == preferred);
        if (!local)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RouteNotConnected,
                $"Preferred actor node '{preferred}' is not connected to this actor directory.");
        }
    }
}
