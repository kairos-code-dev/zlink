using Zlink.Framework.Backend.Contracts;
using System.Security.Cryptography;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotRuntimeManager(
    IServiceProvider services,
    ZLinkFrameworkRuntime runtime,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration)
{
    public void InitializeSpotNodes(ZLinkFrameworkRuntimeState state)
    {
        if (registration.SpotNodes.Count == 0)
        {
            return;
        }

        var channelAdapter = backendAdapterFactory.CreateChannelAdapter();
        var spotAdapter = backendAdapterFactory.CreateSpotAdapter();

        foreach (var spotNodeRegistration in registration.SpotNodes.Values)
        {
            var node = spotAdapter.CreateSpotNode(state.Context);
            node.SetRoutingId(CreateNodeRoutingId(spotNodeRegistration));
            node.Bind(spotNodeRegistration.BindEndpoint!);

            var nodeRuntime = new ZLinkSpotNodeRuntime(
                services,
                runtime,
                registration,
                spotNodeRegistration,
                state.Context,
                channelAdapter,
                node,
                registration.SpotDiscovery?.ChannelName
                    ?? throw new InvalidOperationException("SPOT discovery is not configured."));

            if (registration.SpotDiscovery is not null
                && registration.SpotDiscovery.Endpoints.Count > 0)
            {
                var discovery = CreateDiscovery(
                    channelAdapter,
                    state,
                    registration.SpotDiscovery.ChannelName,
                    ZLinkAutoConnectType.SpotMesh,
                    registration.SpotDiscovery.Endpoints);
                node.AttachDiscovery(discovery);
                nodeRuntime.SpotDiscovery = discovery;
                nodeRuntime.StartDiscoveryPeerReconciliation();
                state.SpotDiscoveries.Add($"{spotNodeRegistration.SpotNodeName}.discovery", discovery);
            }

            foreach (var endpoint in spotNodeRegistration.Router?.ManualConnections ?? [])
            {
                _ = nodeRuntime.ConnectRouterAsync(endpoint, CancellationToken.None);
            }

            foreach (var endpoint in spotNodeRegistration.PubSub?.ManualConnections ?? [])
            {
                _ = nodeRuntime.ConnectPubSubAsync(endpoint, CancellationToken.None);
            }

            foreach (var channelName in spotNodeRegistration.AttachedSpotPublisherClients.Keys)
            {
                nodeRuntime.GetOrCreatePublisherBundle(channelName);
            }

            nodeRuntime.InitializeEntrySpot();
            state.SpotNodes.Add(spotNodeRegistration.SpotNodeName, nodeRuntime);
        }
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

        throw new InvalidOperationException(
            $"SPOT publisher client channel '{channelName}' is not registered.");
    }

    public async ValueTask<ZLinkSpotCreateResult> CreateAsync(
        ZLinkFrameworkRuntimeState state,
        string spotName,
        RoutingId? spotRid,
        CancellationToken cancellationToken)
    {
        foreach (var node in state.SpotNodes.Values)
        {
            if (node.SpotFactories.ContainsKey(spotName))
            {
                return await node.CreateAsync(spotName, spotRid, cancellationToken);
            }
        }

        throw new InvalidOperationException($"SPOT factory '{spotName}' is not registered.");
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

    private static IZLinkBackendDiscovery CreateDiscovery(
        IZLinkChannelBackendAdapter adapter,
        ZLinkFrameworkRuntimeState state,
        string channelName,
        ZLinkAutoConnectType autoConnectType,
        IReadOnlyCollection<string> endpoints)
    {
        var discovery = adapter.CreateDiscovery(state.Context, autoConnectType, channelName);
        foreach (var endpoint in endpoints)
        {
            discovery.ConnectRegistry(endpoint);
        }

        return discovery;
    }

    private static RoutingId CreateNodeRoutingId(ZLinkSpotNodeRegistration registration)
    {
        var bytes = RandomNumberGenerator.GetBytes(16);
        bytes[0] = registration.AttachedSpotPublisherClients.Count switch
        {
            > 0 when registration.SpotFactories.Count == 0 => 0xf0,
            > 0 => 0x80,
            _ => 0x10,
        };
        return RoutingId.FromBytes(bytes);
    }

    private static ZLinkSpotNodeRuntime GetNode(
        ZLinkFrameworkRuntimeState state,
        string spotNodeName)
    {
        return state.SpotNodes.TryGetValue(spotNodeName, out var node)
            ? node
            : throw new InvalidOperationException($"SPOT node '{spotNodeName}' is not registered.");
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
