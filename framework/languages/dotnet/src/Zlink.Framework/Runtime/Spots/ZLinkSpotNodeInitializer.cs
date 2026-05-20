using System.Security.Cryptography;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Core;

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
                registration.SpotDiscovery?.ChannelName ?? spotNodeRegistration.SpotNodeName);

            AttachDiscoveryIfConfigured(state, channelAdapter, spotNodeRegistration, node, nodeRuntime);
            ConnectManualPeers(spotNodeRegistration, nodeRuntime);
            InitializePublisherBundles(spotNodeRegistration, nodeRuntime);

            await nodeRuntime.InitializeEntrySpotAsync().ConfigureAwait(false);
            state.SpotNodes.Add(spotNodeRegistration.SpotNodeName, nodeRuntime);
        }
    }

    private void AttachDiscoveryIfConfigured(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter channelAdapter,
        ZLinkSpotNodeRegistration spotNodeRegistration,
        IZLinkBackendSpotNode node,
        ZLinkSpotNodeRuntime nodeRuntime)
    {
        if (registration.SpotDiscovery is null
            || registration.SpotDiscovery.Endpoints.Count == 0)
        {
            return;
        }

        var discovery = ZLinkBackendDiscoveryFactory.Create(
            channelAdapter,
            state.Context,
            registration.SpotDiscovery.ChannelName,
            ZLinkAutoConnectType.SpotMesh,
            registration.SpotDiscovery.Endpoints);
        if (registration.RegistryActorRoutes is not null)
        {
            discovery.ActorRouteSyncEnabled = true;
        }

        if (registration.RegistrySpotRoutes is not null)
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
        foreach (var endpoint in registration.Router?.ManualConnections ?? [])
        {
            _ = nodeRuntime.ConnectRouterAsync(endpoint, CancellationToken.None);
        }

        foreach (var endpoint in registration.PubSub?.ManualConnections ?? [])
        {
            _ = nodeRuntime.ConnectPubSubAsync(endpoint, CancellationToken.None);
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
        var bytes = RandomNumberGenerator.GetBytes(16);
        bytes[0] = registration.AttachedSpotPublisherClients.Count switch
        {
            > 0 when registration.SpotFactories.Count == 0 => 0xf0,
            > 0 => 0x80,
            _ => 0x10,
        };
        return RoutingId.FromBytes(bytes);
    }
}
