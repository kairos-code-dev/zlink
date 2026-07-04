namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotRuntimeManager(
    IServiceProvider services,
    ZLinkFrameworkRuntime runtime,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration)
{
    private readonly ZLinkEntrySpotActorRouter _entrySpotActors = new(runtime);

    private readonly ZLinkSpotNodeInitializer _nodeInitializer = new(
        services,
        runtime,
        backendAdapterFactory,
        registration);

    public async ValueTask InitializeSpotNodesAsync(ZLinkFrameworkRuntimeState state)
    {
        await _nodeInitializer.InitializeAsync(state).ConfigureAwait(false);
    }

    public ZLinkSpotPublisherBundle GetPublisherBundle(
        ZLinkFrameworkRuntimeState state,
        string channelName)
    {
        if (state.SpotNodes.TryGetValue(channelName, out var node))
        {
            if (node.TryGetPublisherBundle(channelName, out var bundle)) return bundle;

            return node.GetOrCreatePublisherBundle(channelName);
        }

        throw new ZLinkConfigurationException(
            $"SPOT publisher mesh '{channelName}' is not registered.");
    }

    public async ValueTask<ZLinkSpotCreateResult> CreateAsync(
        ZLinkFrameworkRuntimeState state,
        Type spotType,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var node = GetNodeForSpotFactory(state, spotType);
        return await node.CreateAsync(spotType, request, cancellationToken);
    }

    public async ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync(
        ZLinkFrameworkRuntimeState state,
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
        ZLinkFrameworkRuntimeState state,
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
        ZLinkFrameworkRuntimeState state,
        CancellationToken cancellationToken)
    {
        var results = new List<ZLinkSpotInfo>();
        foreach (var node in state.SpotNodes.Values) results.AddRange(await node.ListAsync(cancellationToken));

        return results
            .OrderBy(static info => info.SpotRid.ToHex(), StringComparer.Ordinal)
            .ToArray();
    }

    public async ValueTask<bool> CloseAsync(
        ZLinkFrameworkRuntimeState state,
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        foreach (var node in state.SpotNodes.Values)
            if (await node.CloseAsync(spotRid, cancellationToken))
                return true;

        return false;
    }

    public async ValueTask<ZLinkSpotActorJoinResult> JoinActorAsync(
        ZLinkFrameworkRuntimeState state,
        RoutingId spotRid,
        IZLinkActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var activation = GetActivation(state, spotRid)
                         ?? throw new InvalidOperationException($"SPOT '{spotRid}' is not active.");

        return await activation.JoinActorAsync(actor, request, cancellationToken);
    }

    public async ValueTask<bool> TrySubmitEntrySpotActorAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        return await _entrySpotActors.TryAsync(
                state,
                actor,
                runtimeState,
                header,
                body,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<EntrySpotActorReplyDispatchResult> TrySubmitEntrySpotActorForReplyAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        bool callerOwnsDispatchTurn,
        CancellationToken cancellationToken)
    {
        return await _entrySpotActors.TrySubmitForReplyAsync(
                state,
                actor,
                runtimeState,
                header,
                body,
                callerOwnsDispatchTurn,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask SubmitResolvedEntrySpotActorAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Func<CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        await _entrySpotActors.SubmitResolvedAsync(
                actor,
                runtimeState,
                header,
                operation,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask NotifyEntrySpotActorJoinedAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        RoutingId? targetNodeRid,
        CancellationToken cancellationToken)
    {
        await _entrySpotActors.NotifyJoinedAsync(state, actor, targetNodeRid, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask NotifyEntrySpotActorCreatedAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        ZLinkMessage createRequest,
        RoutingId? targetNodeRid,
        CancellationToken cancellationToken)
    {
        await _entrySpotActors.NotifyCreatedAsync(
                state,
                actor,
                createRequest,
                targetNodeRid,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask NotifyEntrySpotActorLeftAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        RoutingId? targetNodeRid,
        CancellationToken cancellationToken)
    {
        await _entrySpotActors.NotifyLeftAsync(state, actor, targetNodeRid, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<bool> TryNotifyEntrySpotActorDisconnectedAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        RoutingId? targetNodeRid,
        CancellationToken cancellationToken)
    {
        return await _entrySpotActors.TryNotifyDisconnectedAsync(
                state,
                actor,
                targetNodeRid,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<bool> TryNotifyJoinedSpotActorDisconnectedAsync(
        ZLinkFrameworkRuntimeState state,
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
        ZLinkFrameworkRuntimeState state,
        string spotNodeName)
    {
        return GetNode(state, spotNodeName).GetMonitoringSnapshot();
    }

    private static ZLinkSpotNodeRuntime GetNode(
        ZLinkFrameworkRuntimeState state,
        string spotNodeName)
    {
        return state.SpotNodes.TryGetValue(spotNodeName, out var node)
            ? node
            : throw new ZLinkConfigurationException($"SPOT node '{spotNodeName}' is not registered.");
    }

    private static ZLinkSpotNodeRuntime GetNodeForSpotFactory(
        ZLinkFrameworkRuntimeState state,
        Type spotType)
    {
        foreach (var node in state.SpotNodes.Values)
            if (node.SpotFactories.Contains(spotType))
                return node;

        throw new ZLinkConfigurationException($"SPOT factory '{spotType}' is not registered.");
    }

    public ZLinkSpotActivation? GetActivationBySpotRid(
        ZLinkFrameworkRuntimeState state,
        RoutingId spotRid)
    {
        return GetActivation(state, spotRid);
    }

    private static ZLinkSpotActivation? GetActivation(
        ZLinkFrameworkRuntimeState state,
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
