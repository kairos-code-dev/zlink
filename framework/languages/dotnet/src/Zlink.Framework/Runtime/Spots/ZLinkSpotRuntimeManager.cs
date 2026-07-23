namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotRuntimeManager(
    IServiceProvider services,
    ZLinkFrameworkRuntime runtime,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration,
    ZLinkLocationLifecycle? locationLifecycle)
{
    private readonly ZLinkEntrySpotActorRouter _entrySpotActors = new(runtime);

    private readonly ZLinkSpotNodeInitializer _nodeInitializer = new(
        services,
        runtime,
        backendAdapterFactory,
        registration,
        locationLifecycle);

    public ZLinkEntrySpotActorRouter EntrySpotActors => _entrySpotActors;

    public async ValueTask InitializeSpotNodesAsync(ZLinkFrameworkComponentState state)
    {
        await _nodeInitializer.InitializeAsync(state).ConfigureAwait(false);
    }

    public ZLinkSpotPublisherBundle GetPublisherBundle(
        ZLinkFrameworkComponentState state,
        string channelName)
    {
        if (state.SpotNodes.TryGetValue(channelName, out var node))
            return node.GetOrCreatePublisherBundle(channelName);

        throw new ZLinkConfigurationException(
            $"SPOT publisher mesh '{channelName}' is not registered.");
    }

    public async ValueTask<ZLinkSpotCreateResult> CreateAsync(
        ZLinkFrameworkComponentState state,
        Type spotType,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var node = GetNodeForSpotFactory(state, spotType);
        return await node.CreateAsync(spotType, request, cancellationToken);
    }

    public async ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync(
        ZLinkFrameworkComponentState state,
        Type spotType,
        RoutingId spotRid,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var node = GetNodeForSpotFactory(state, spotType);
        return await node.GetOrCreateAsync(
            spotType,
            spotRid,
            request,
            cancellationToken);
    }

    public async ValueTask<ZLinkSpotInfo?> GetAsync(
        ZLinkFrameworkComponentState state,
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        foreach (var node in state.SpotNodes.Values)
        {
            var info = await node.GetAsync(spotRid, cancellationToken);
            if (info is not null) return info;
        }

        return null;
    }

    public async ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(
        ZLinkFrameworkComponentState state,
        CancellationToken cancellationToken)
    {
        var results = new List<ZLinkSpotInfo>();
        foreach (var node in state.SpotNodes.Values) results.AddRange(await node.ListAsync(cancellationToken));

        return results
            .OrderBy(static info => info.SpotRid.ToHex(), StringComparer.Ordinal)
            .ToArray();
    }

    public async ValueTask<bool> CloseAsync(
        ZLinkFrameworkComponentState state,
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        foreach (var node in state.SpotNodes.Values)
            if (await node.CloseAsync(spotRid, cancellationToken))
                return true;

        return false;
    }

    public async ValueTask<ZLinkSpotActorJoinResult> JoinActorAsync(
        ZLinkFrameworkComponentState state,
        RoutingId spotRid,
        IZLinkActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var activation = GetActivationBySpotRid(state, spotRid)
                         ?? throw new InvalidOperationException($"SPOT '{spotRid}' is not active.");

        return await activation.JoinActorAsync(actor, request, cancellationToken);
    }

    public async ValueTask<bool> TryNotifyJoinedSpotActorDisconnectedAsync(
        ZLinkFrameworkComponentState state,
        string actorId,
        CancellationToken cancellationToken)
    {
        foreach (var activation in state.SpotNodes.Values.SelectMany(static node => node.Spots))
        {
            if (!activation.TryGetJoinedActor(actorId, out var actor) || actor is null) continue;

            await activation.NotifyActorDisconnectedAsync(actor, cancellationToken)
                .ConfigureAwait(false);
            return true;
        }

        return false;
    }

    public ZLinkSpotMonitoringSnapshot GetMonitoringSnapshot(
        ZLinkFrameworkComponentState state,
        string spotNodeName)
    {
        return GetNode(state, spotNodeName).GetMonitoringSnapshot();
    }

    private static ZLinkSpotNodeRuntime GetNode(
        ZLinkFrameworkComponentState state,
        string spotNodeName)
    {
        return state.SpotNodes.TryGetValue(spotNodeName, out var node)
            ? node
            : throw new ZLinkConfigurationException($"SPOT node '{spotNodeName}' is not registered.");
    }

    private static ZLinkSpotNodeRuntime GetNodeForSpotFactory(
        ZLinkFrameworkComponentState state,
        Type spotType)
    {
        foreach (var node in state.SpotNodes.Values)
            if (node.SpotFactories.Contains(spotType))
                return node;

        throw new ZLinkConfigurationException($"SPOT factory '{spotType}' is not registered.");
    }

    public ZLinkSpotActivation? GetActivationBySpotRid(
        ZLinkFrameworkComponentState state,
        RoutingId spotRid)
    {
        foreach (var node in state.SpotNodes.Values)
        {
            var activation = node.Spots.FirstOrDefault(current => current.SpotRid == spotRid);
            if (activation is not null) return activation;
        }

        return null;
    }
}

internal readonly record struct EntrySpotActorReplyDispatchResult(
    bool Handled,
    ZLinkActorReply? Reply);
