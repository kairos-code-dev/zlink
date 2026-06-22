using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Execution;
using Zlink.Framework.Runtime.Host;
using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Registry;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelBundleFactory(
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration)
{
    public ZLinkChannelRuntimeBundle CreateClientBundle(
        ZLinkFrameworkRuntimeState state,
        string channelName,
        ZLinkChannelRegistration channel)
    {
        var adapter = backendAdapterFactory.CreateChannelAdapter();
        var dealer = adapter.CreateDealerSocket(state.Context);
        dealer.SetChannelName(channelName);
        // weight 는 bind/connect/discovery 前에 적용해 default-weight 노출 창을 없앤다.
        dealer.SetPeerWeight(channel.Client!.SocketConfig.Weight);
        if (!string.IsNullOrWhiteSpace(channel.Client.BindEndpoint))
        {
            dealer.Bind(channel.Client.BindEndpoint);
        }

        var bundle = new ZLinkChannelRuntimeBundle(
            dealer,
            new ZLinkAsyncSubmitter(
                dealer.OnSendReady,
                channel.Client.SocketConfig.SendTimeout ?? registration.DefaultSocketSendTimeout,
                state.StopTokenSource.Token));

        try
        {
            if (channel.Client.ManualConnections.Count > 0)
            {
                AttachManualConnections(bundle, dealer, channel.Client.ManualConnections);
            }
            else
            {
                AttachDiscovery(
                    bundle,
                    adapter,
                    state.Context,
                    channelName,
                    ResolveClientAutoConnectType(channel),
                    registration.Discovery?.Endpoints ?? [],
                    dealer.AttachDiscovery);
            }

            return bundle;
        }
        catch
        {
            _ = DisposeFailedBundleAsync(bundle);
            throw;
        }
    }

    public ZLinkChannelRuntimeBundle CreateServerBundle(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter,
        string channelName,
        ZLinkChannelRegistration channel)
    {
        var router = adapter.CreateRouterSocket(state.Context);
        router.SetChannelName(channelName);
        if (channel.Server!.RoutingConfig.RoutingId.Size > 0)
        {
            router.SetRoutingId(channel.Server.RoutingConfig.RoutingId);
        }
        // weight 는 bind 前에 적용해 default-weight 노출 창을 없앤다(peer 가 그 사이 연결할 수 있다).
        router.SetPeerWeight(channel.Server.SocketConfig.Weight);
        router.Bind(channel.Server!.BindEndpoint!);
        var bundle = new ZLinkChannelRuntimeBundle(router);

        if (registration.Discovery is not null)
        {
            AttachDiscovery(
                bundle,
                adapter,
                state.Context,
                channelName,
                ZLinkAutoConnectType.ClientServer,
                registration.Discovery.Endpoints,
                router.AttachDiscovery);
        }

        return bundle;
    }

    public ZLinkChannelRuntimeBundle CreateSubscriberBundle(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter,
        string channelName,
        ZLinkChannelRegistration channel)
    {
        var subscriber = adapter.CreateSubscriberSocket(state.Context);
        subscriber.SetChannelName(channelName);
        subscriber.SetSubscription(string.Empty);
        var bundle = new ZLinkChannelRuntimeBundle(subscriber);

        if (channel.Subscriber!.ManualConnections.Count > 0)
        {
            AttachManualConnections(bundle, subscriber, channel.Subscriber.ManualConnections);
        }
        else
        {
            AttachDiscovery(
                bundle,
                adapter,
                state.Context,
                channelName,
                ZLinkAutoConnectType.Fanout,
                registration.Discovery?.Endpoints ?? [],
                subscriber.AttachDiscovery);
        }

        return bundle;
    }

    public ZLinkChannelRuntimeBundle CreatePublisherBundle(
        ZLinkFrameworkRuntimeState state,
        string channelName,
        ZLinkChannelRegistration channel,
        IZLinkChannelBackendAdapter? adapter = null)
    {
        adapter ??= backendAdapterFactory.CreateChannelAdapter();
        var publisher = adapter.CreatePublisherSocket(state.Context);
        publisher.SetChannelName(channelName);
        publisher.Bind(channel.Publisher!.BindEndpoint!);
        var bundle = new ZLinkChannelRuntimeBundle(
            publisher,
            new ZLinkAsyncSubmitter(
                publisher.OnSendReady,
                channel.Publisher.SocketConfig.SendTimeout ?? registration.DefaultSocketSendTimeout,
                state.StopTokenSource.Token));

        if (registration.Discovery is not null)
        {
            AttachDiscovery(
                bundle,
                adapter,
                state.Context,
                channelName,
                ZLinkAutoConnectType.Fanout,
                registration.Discovery.Endpoints,
                publisher.AttachDiscovery);
        }

        return bundle;
    }

    private static ZLinkAutoConnectType ResolveClientAutoConnectType(ZLinkChannelRegistration channel)
    {
        return channel.AutoConnectType == ZLinkAutoConnectType.DealerMesh
            ? ZLinkAutoConnectType.DealerMesh
            : ZLinkAutoConnectType.ClientServer;
    }

    private static void AttachManualConnections(
        ZLinkChannelRuntimeBundle bundle,
        IZLinkBackendConnectableSocket socket,
        IReadOnlyList<string> endpoints)
    {
        foreach (var endpoint in endpoints)
        {
            socket.Connect(endpoint);
            _ = bundle.TryAddManualConnection(endpoint);
        }
    }

    private static void AttachDiscovery(
        ZLinkChannelRuntimeBundle bundle,
        IZLinkChannelBackendAdapter adapter,
        IZLinkBackendContext context,
        string channelName,
        ZLinkAutoConnectType autoConnectType,
        IReadOnlyList<string> endpoints,
        Action<IZLinkBackendDiscovery> attachDiscovery)
    {
        var discovery = ZLinkBackendDiscoveryFactory.Create(
            adapter,
            context,
            channelName,
            autoConnectType,
            endpoints);
        attachDiscovery(discovery);
        bundle.Discovery = discovery;
    }

    private static async ValueTask DisposeFailedBundleAsync(ZLinkChannelRuntimeBundle bundle)
    {
        await bundle.DisposeAsync().ConfigureAwait(false);
    }
}
