using System.Security.Cryptography;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Execution;
using Zlink.Framework.Runtime.Host;
using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Registry;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotNodeInitializer(
    IServiceProvider services,
    ZLinkFrameworkRuntime runtime,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration)
{
    public async ValueTask InitializeAsync(ZLinkFrameworkRuntimeState state)
    {
        if (registration.SpotNodes.Count == 0)
        {
            return;
        }

        var channelAdapter = backendAdapterFactory.CreateChannelAdapter();
        var spotAdapter = backendAdapterFactory.CreateSpotAdapter();

        foreach (var spotNodeRegistration in registration.SpotNodes.Values)
        {
            var node = spotAdapter.CreateSpotNode(
                state.Context,
                ResolveSpotNodeMode(spotNodeRegistration));
            node.SetRoutingId(CreateNodeRoutingId(spotNodeRegistration));
            var nodeRuntime = new ZLinkSpotNodeRuntime(
                services,
                runtime,
                registration,
                spotNodeRegistration,
                state.Context,
                channelAdapter,
                node,
                registration.SpotDiscovery?.ChannelName ?? spotNodeRegistration.SpotNodeName);

            nodeRuntime.ApplyEntrySpotRoutingIdBeforeBind();
            if (spotNodeRegistration.Router is not null
                && spotNodeRegistration.Router.BindEndpoint is { Length: > 0 } routerEndpoint)
            {
                node.SetRouterBind(routerEndpoint);
            }
            if (spotNodeRegistration.PubSub is not null
                && spotNodeRegistration.PubSub.BindEndpoint is { Length: > 0 } pubEndpoint)
            {
                node.SetPubBind(pubEndpoint);
            }

            AttachDiscoveryIfConfigured(state, channelAdapter, spotNodeRegistration, node, nodeRuntime);
            ConnectManualPeers(spotNodeRegistration, nodeRuntime);
            AttachAcceptedSpotRouteChannels(state, channelAdapter, spotNodeRegistration, node);
            InitializePublisherBundles(spotNodeRegistration, nodeRuntime);

            await nodeRuntime.InitializeEntrySpotAsync().ConfigureAwait(false);
            state.SpotNodes.Add(spotNodeRegistration.SpotNodeName, nodeRuntime);
        }
    }

    private static SpotNodeMode ResolveSpotNodeMode(ZLinkSpotNodeRegistration registration)
    {
        return (registration.Router is not null, registration.PubSub is not null) switch
        {
            (true, true) => SpotNodeMode.All,
            (true, false) => SpotNodeMode.Routed,
            (false, true) => SpotNodeMode.PubSub,
            _ => throw new ZLinkConfigurationException(
                $"SPOT node '{registration.SpotNodeName}' must enable router or pub/sub capability.")
        };
    }

    private void AttachDiscoveryIfConfigured(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter channelAdapter,
        ZLinkSpotNodeRegistration spotNodeRegistration,
        IZLinkBackendSpotNode node,
        ZLinkSpotNodeRuntime nodeRuntime)
    {
        if (registration.SpotDiscovery is null)
        {
            return;
        }

        var endpoints = ZLinkFrameworkRegistrationValidator.ResolveSpotDiscoveryEndpoints(registration);
        if (endpoints.Count == 0)
        {
            return;
        }

        var discovery = ZLinkBackendDiscoveryFactory.Create(
            channelAdapter,
            state.Context,
            registration.SpotDiscovery.ChannelName,
            ZLinkAutoConnectType.SpotMesh,
            endpoints);
        if (registration.RegistrySpotRemoteAddresses is not null)
        {
            discovery.SpotOwnerSyncEnabled = true;
        }
        node.AttachDiscovery(discovery);
        nodeRuntime.SpotDiscovery = discovery;
        nodeRuntime.StartDiscoveryPeerReconciliation();
        state.SpotDiscoveries.Add($"{spotNodeRegistration.SpotNodeName}.discovery", discovery);
    }

    private static void ConnectManualPeers(
        ZLinkSpotNodeRegistration registration,
        ZLinkSpotNodeRuntime nodeRuntime)
    {
        foreach (var connection in registration.Router?.ManualConnections ?? [])
        {
            _ = connection.PeerRid is { } peerRid
                ? nodeRuntime.ConnectRouterAsync(peerRid, connection.Endpoint, CancellationToken.None)
                : nodeRuntime.ConnectRouterAsync(connection.Endpoint, CancellationToken.None);
        }

        foreach (var endpoint in registration.PubSub?.ManualConnections ?? [])
        {
            _ = nodeRuntime.ConnectPubSubAsync(endpoint, CancellationToken.None);
        }
    }

    private void AttachAcceptedSpotRouteChannels(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter channelAdapter,
        ZLinkSpotNodeRegistration spotNodeRegistration,
        IZLinkBackendSpotNode node)
    {
        foreach (var acceptance in spotNodeRegistration.AcceptedSpotRouteChannels.Values)
        {
            if (registration.RouteChannels.ContainsKey(acceptance.ChannelName))
            {
                continue;
            }

            if (registration.Channels.ContainsKey(acceptance.ChannelName))
            {
                AttachAcceptedSpotRouteServerBundle(state, acceptance, node);
                continue;
            }

            throw new ZLinkConfigurationException(
                $"Accepted SPOT route channel '{acceptance.ChannelName}' is not registered.");
        }
    }

    private static void AttachAcceptedSpotRouteServerBundle(
        ZLinkFrameworkRuntimeState state,
        ZLinkSpotRouteChannelAcceptanceRegistration acceptance,
        IZLinkBackendSpotNode node)
    {
        if (!state.ServerBundles.TryGetValue(acceptance.ChannelName, out var bundle)
            || bundle.Socket is not IZLinkBackendRouterSocket router)
        {
            throw new ZLinkConfigurationException(
                $"Accepted SPOT route channel '{acceptance.ChannelName}' must have a server router socket in this process.");
        }

        if (bundle.SpotRouteBridge is not null)
        {
            throw new ZLinkConfigurationException(
                $"Accepted SPOT route channel '{acceptance.ChannelName}' is already attached to a SPOT route bridge.");
        }

        var bridge = node.CreateRouteBridge();
        try
        {
            bridge.AttachRouterChannel(
                acceptance.ChannelName,
                router,
                new SpotRouteBridgeEndpointOptions
                {
                    Capabilities = SpotRouteBridgeEndpointCapabilities.RouteWithChannelInbound
                });
        }
        catch
        {
            _ = bridge.DisposeAsync();
            throw;
        }

        bundle.SpotRouteBridge = bridge;

        foreach (var endpoint in acceptance.ManualConnections)
        {
            router.Connect(endpoint);
        }
    }

    private static void InitializePublisherBundles(
        ZLinkSpotNodeRegistration registration,
        ZLinkSpotNodeRuntime nodeRuntime)
    {
        foreach (var channelName in registration.AttachedSpotPublisherClients.Keys)
        {
            nodeRuntime.GetOrCreatePublisherBundle(channelName);
        }
    }

    private static RoutingId CreateNodeRoutingId(ZLinkSpotNodeRegistration registration)
    {
        if (registration.Router?.RoutingConfig.RoutingId.Size > 0)
        {
            return registration.Router.RoutingConfig.RoutingId;
        }
        if (registration.PubSub?.RoutingId.Size > 0)
        {
            return registration.PubSub.RoutingId;
        }

        var bytes = RandomNumberGenerator.GetBytes(16);
        bytes[0] = registration.AttachedSpotPublisherClients.Count switch
        {
            > 0 when registration.SpotFactories.Count == 0 => 0xf0,
            > 0 => 0x80,
            _ => 0x10,
        };
        return RoutingId.From(bytes);
    }
}
