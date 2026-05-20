using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotRuntimeManager(
    IServiceProvider services,
    ZLinkFrameworkRuntime runtime,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration)
{
    private readonly ZLinkSpotNodeInitializer _nodeInitializer = new(
        services,
        runtime,
        backendAdapterFactory,
        registration);
    private readonly ZLinkEntrySpotActorRouter _entrySpotActors = new();

    public async ValueTask InitializeSpotNodesAsync(ZLinkFrameworkRuntimeState state)
    {
        await _nodeInitializer.InitializeAsync(state).ConfigureAwait(false);
    }

    public ZLinkSpotPublisherBundle GetPublisherBundle(
        ZLinkFrameworkRuntimeState state,
        string channelName)
    {
        foreach (var node in state.SpotNodes.Values)
        {
            if (node.PublisherBundles.TryGetValue(channelName, out var bundle))
            {
                return bundle;
            }

            if (node.HasPublisherClient(channelName))
            {
                return node.GetOrCreatePublisherBundle(channelName);
            }
        }

        throw new ZLinkConfigurationException(
            $"SPOT publisher client channel '{channelName}' is not registered.");
    }

    public async ValueTask<ZLinkSpotCreateResult> CreateAsync(
        ZLinkFrameworkRuntimeState state,
        string spotName,
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        var node = GetNodeForSpotFactory(state, spotName);
        return await node.CreateAsync(spotName, createParts, cancellationToken);
    }

    public async ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync(
        ZLinkFrameworkRuntimeState state,
        string spotName,
        RoutingId spotRid,
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        var node = GetNodeForSpotFactory(state, spotName);
        return await node.GetOrCreateAsync(
            spotName,
            spotRid,
            createParts,
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
            if (info is not null)
            {
                return info;
            }
        }

        return null;
    }

    public async ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(
        ZLinkFrameworkRuntimeState state,
        CancellationToken cancellationToken)
    {
        var results = new List<ZLinkSpotInfo>();
        foreach (var node in state.SpotNodes.Values)
        {
            results.AddRange(await node.ListAsync(cancellationToken));
        }

        return results
            .OrderBy(static info => info.SpotName, StringComparer.Ordinal)
            .ToArray();
    }

    public async ValueTask<bool> RemoveAsync(
        ZLinkFrameworkRuntimeState state,
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        foreach (var node in state.SpotNodes.Values)
        {
            if (await node.RemoveAsync(spotRid, cancellationToken))
            {
                return true;
            }
        }

        return false;
    }

    public async ValueTask<TReply> JoinActorAsync<TRequest, TReply>(
        ZLinkFrameworkRuntimeState state,
        RoutingId spotRid,
        IZLinkActor actor,
        TRequest request,
        CancellationToken cancellationToken)
    {
        var activation = GetActivation(state, spotRid)
            ?? throw new InvalidOperationException($"SPOT '{spotRid}' is not active.");

        return await activation.JoinActorAsync<TRequest, TReply>(actor, request, cancellationToken);
    }

    public async ValueTask<bool> TrySubmitEntrySpotActorAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        return await _entrySpotActors.TrySubmitAsync(
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
        CancellationToken cancellationToken)
    {
        return await _entrySpotActors.TrySubmitForReplyAsync(
                state,
                actor,
                runtimeState,
                header,
                body,
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
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        await _entrySpotActors.NotifyJoinedAsync(state, actor, info, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask NotifyEntrySpotActorLeftAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        await _entrySpotActors.NotifyLeftAsync(state, actor, info, cancellationToken)
            .ConfigureAwait(false);
    }

    public IZLinkEndpointConnections GetRouterConnections(
        ZLinkFrameworkRuntimeState state,
        string spotNodeName)
    {
        var node = GetNode(state, spotNodeName);
        return new ZLinkRuntimeConnections(
            (endpoint, token) => node.ConnectRouterAsync(endpoint, token),
            (endpoint, _) =>
            {
                node.DisconnectRouter(endpoint);
                return ValueTask.CompletedTask;
            },
            _ => ValueTask.FromResult<IReadOnlyList<string>>(node.ListRouterConnections()));
    }

    public IZLinkEndpointConnections GetPubSubConnections(
        ZLinkFrameworkRuntimeState state,
        string spotNodeName)
    {
        var node = GetNode(state, spotNodeName);
        return new ZLinkRuntimeConnections(
            (endpoint, token) => node.ConnectPubSubAsync(endpoint, token),
            (endpoint, _) =>
            {
                node.DisconnectPubSub(endpoint);
                return ValueTask.CompletedTask;
            },
            _ => ValueTask.FromResult<IReadOnlyList<string>>(node.ListPubSubConnections()));
    }

    public IZLinkEndpointConnections GetChannelClientConnections(
        ZLinkFrameworkRuntimeState state,
        string spotNodeName,
        string channelName)
    {
        var node = GetNode(state, spotNodeName);
        if (!node.AttachedChannelBundles.TryGetValue(channelName, out var bundle))
        {
            bundle = node.GetOrCreateAttachedChannelBundle(channelName);
        }

        return new ZLinkRuntimeConnections(
            (endpoint, _) =>
            {
                if (!bundle.TryAddManualConnection(endpoint))
                {
                    return ValueTask.FromResult(false);
                }

                bundle.Socket.Connect(endpoint);
                return ValueTask.FromResult(true);
            },
            (endpoint, _) =>
            {
                bundle.Socket.Disconnect(endpoint);
                bundle.RemoveManualConnection(endpoint);
                return ValueTask.CompletedTask;
            },
            _ => ValueTask.FromResult<IReadOnlyList<string>>(bundle.ListManualConnections()));
    }

    public IZLinkEndpointConnections GetPublisherConnections(
        ZLinkFrameworkRuntimeState state,
        string spotNodeName,
        string channelName)
    {
        var node = GetNode(state, spotNodeName);
        if (!node.PublisherBundles.TryGetValue(channelName, out var bundle))
        {
            bundle = node.GetOrCreatePublisherBundle(channelName);
        }

        _ = bundle;

        return new ZLinkRuntimeConnections(
            (endpoint, token) => node.ConnectPubSubAsync(endpoint, token),
            (endpoint, _) =>
            {
                node.DisconnectPubSub(endpoint);
                return ValueTask.CompletedTask;
            },
            _ => ValueTask.FromResult<IReadOnlyList<string>>(node.ListPubSubConnections()));
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
        string spotName)
    {
        foreach (var node in state.SpotNodes.Values)
        {
            if (node.SpotFactories.ContainsKey(spotName))
            {
                return node;
            }
        }

        throw new ZLinkConfigurationException($"SPOT factory '{spotName}' is not registered.");
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
            if (activation is not null)
            {
                return activation;
            }
        }

        return null;
    }
}

internal readonly record struct EntrySpotActorReplyDispatchResult(
    bool Handled,
    byte[]? Reply);
