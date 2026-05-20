using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Core;

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
        if (!string.IsNullOrWhiteSpace(channel.Client!.BindEndpoint))
        {
            dealer.Bind(channel.Client.BindEndpoint);
        }

        var bundle = new ZLinkChannelRuntimeBundle(
            dealer,
            new ZLinkAsyncSubmitter(
                dealer.OnSendReady,
                channel.Client.SocketConfig.SendTimeout,
                state.StopTokenSource.Token));

        try
        {
            if (channel.Client.ManualConnections.Count > 0)
            {
                foreach (var endpoint in channel.Client.ManualConnections)
                {
                    dealer.Connect(endpoint);
                    _ = bundle.TryAddManualConnection(endpoint);
                }
            }
            else
            {
                var discovery = ZLinkBackendDiscoveryFactory.Create(
                    adapter,
                    state.Context,
                    channelName,
                    ResolveClientAutoConnectType(channel),
                    registration.Discovery?.Endpoints ?? []);
                dealer.AttachDiscovery(discovery);
                bundle.Discovery = discovery;
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

        router.Bind(channel.Server!.BindEndpoint!);
        var bundle = new ZLinkChannelRuntimeBundle(router);

        if (registration.Discovery is not null)
        {
            var discovery = ZLinkBackendDiscoveryFactory.Create(
                adapter,
                state.Context,
                channelName,
                ZLinkAutoConnectType.ClientServer,
                registration.Discovery.Endpoints);
            router.AttachDiscovery(discovery);
            bundle.Discovery = discovery;
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
            foreach (var endpoint in channel.Subscriber.ManualConnections)
            {
                subscriber.Connect(endpoint);
                _ = bundle.TryAddManualConnection(endpoint);
            }
        }
        else
        {
            var discovery = ZLinkBackendDiscoveryFactory.Create(
                adapter,
                state.Context,
                channelName,
                ZLinkAutoConnectType.Fanout,
                registration.Discovery?.Endpoints ?? []);
            subscriber.AttachDiscovery(discovery);
            bundle.Discovery = discovery;
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
                channel.Publisher.SocketConfig.SendTimeout,
                state.StopTokenSource.Token));

        if (registration.Discovery is not null)
        {
            var discovery = ZLinkBackendDiscoveryFactory.Create(
                adapter,
                state.Context,
                channelName,
                ZLinkAutoConnectType.Fanout,
                registration.Discovery.Endpoints);
            publisher.AttachDiscovery(discovery);
            bundle.Discovery = discovery;
        }

        return bundle;
    }

    private static ZLinkAutoConnectType ResolveClientAutoConnectType(ZLinkChannelRegistration channel)
    {
        return channel.AutoConnectType == ZLinkAutoConnectType.DealerMesh
            ? ZLinkAutoConnectType.DealerMesh
            : ZLinkAutoConnectType.ClientServer;
    }

    private static async ValueTask DisposeFailedBundleAsync(ZLinkChannelRuntimeBundle bundle)
    {
        await bundle.DisposeAsync().ConfigureAwait(false);
    }
}
