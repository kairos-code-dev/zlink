using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelBundleFactory(
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration)
{
    public async ValueTask<ZLinkChannelRuntimeBundle> CreateClientBundleAsync(
        ZLinkFrameworkRuntimeState state,
        string channelName,
        ZLinkChannelRegistration channel)
    {
        var adapter = backendAdapterFactory.CreateChannelAdapter();
        IZLinkBackendDealerSocket? dealer = null;
        ZLinkChannelRuntimeBundle? bundle = null;
        try
        {
            dealer = adapter.CreateDealerSocket(state.Context);
            dealer.SetChannelName(channelName);
            ApplySocketConfig(dealer, channel.Client!.SocketConfig);
            RoutingId localRid = default;
            if (channel.RoutingId.Size > 0)
            {
                localRid = ZLinkRoutingIdPolicy.Derive(channel.RoutingId, "dealer");
                dealer.SetRoutingId(localRid);
            }

            // weight 는 bind/connect 前에 적용해 default-weight 노출 창을 없앤다.
            dealer.SetPeerWeight(channel.Client.SocketConfig.Weight);
            if (!string.IsNullOrWhiteSpace(channel.Client.BindEndpoint)) dealer.Bind(channel.Client.BindEndpoint);

            bundle = new ZLinkChannelRuntimeBundle(
                socket: dealer,
                submitter: new ZLinkAsyncSubmitter(
                    dealer.OnSendReady,
                    channel.Client.SocketConfig.SendTimeout ?? registration.DefaultSocketSendTimeout,
                    state.StopTokenSource.Token),
                completionPump: ZLinkRequestCompletionPump.Start(dealer.NativeInstance),
                localRid: localRid,
                socketRole: "dealer");

            AttachManualConnections(bundle, dealer, channel.Client.ManualConnections);

            return bundle;
        }
        catch
        {
            if (bundle is not null)
                await bundle.DisposeAsync().ConfigureAwait(false);
            else if (dealer is not null)
                await dealer.DisposeAsync().ConfigureAwait(false);

            throw;
        }
    }

    public async ValueTask<ZLinkChannelRuntimeBundle> CreateServerBundleAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter,
        string channelName,
        ZLinkChannelRegistration channel)
    {
        IZLinkBackendRouterSocket? router = null;
        ZLinkChannelRuntimeBundle? bundle = null;
        try
        {
            router = adapter.CreateRouterSocket(state.Context);
            router.SetChannelName(channelName);
            ApplySocketConfig(router, channel.Server!.SocketConfig);
            RoutingId localRid = default;
            if (channel.RoutingId.Size > 0)
            {
                localRid = channel.RoutingId;
                router.SetRoutingId(localRid);
            }

            // weight 는 bind 前에 적용해 default-weight 노출 창을 없앤다(peer 가 그 사이 연결할 수 있다).
            router.SetPeerWeight(channel.Server.SocketConfig.Weight);
            router.Bind(channel.Server!.BindEndpoint!);
            bundle = new ZLinkChannelRuntimeBundle(router, localRid: localRid, socketRole: "router");

            return bundle;
        }
        catch
        {
            if (bundle is not null)
                await bundle.DisposeAsync().ConfigureAwait(false);
            else if (router is not null)
                await router.DisposeAsync().ConfigureAwait(false);

            throw;
        }
    }

    public async ValueTask<ZLinkChannelRuntimeBundle> CreateSubscriberBundleAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter,
        string channelName,
        ZLinkChannelRegistration channel)
    {
        IZLinkBackendSubscriberSocket? subscriber = null;
        ZLinkChannelRuntimeBundle? bundle = null;
        try
        {
            subscriber = adapter.CreateSubscriberSocket(state.Context);
            subscriber.SetChannelName(channelName);
            ApplySocketConfig(subscriber, channel.Subscriber!.SocketConfig);
            RoutingId localRid = default;
            if (channel.RoutingId.Size > 0)
            {
                localRid = ZLinkRoutingIdPolicy.Derive(channel.RoutingId, "sub");
                subscriber.SetRoutingId(localRid);
            }

            subscriber.SetSubscription(string.Empty);
            bundle = new ZLinkChannelRuntimeBundle(subscriber, localRid: localRid, socketRole: "sub");

            AttachManualConnections(bundle, subscriber, channel.Subscriber.ManualConnections);

            return bundle;
        }
        catch
        {
            if (bundle is not null)
                await bundle.DisposeAsync().ConfigureAwait(false);
            else if (subscriber is not null)
                await subscriber.DisposeAsync().ConfigureAwait(false);

            throw;
        }
    }

    public async ValueTask<ZLinkChannelRuntimeBundle> CreatePublisherBundleAsync(
        ZLinkFrameworkRuntimeState state,
        string channelName,
        ZLinkChannelRegistration channel,
        IZLinkChannelBackendAdapter? adapter = null)
    {
        adapter ??= backendAdapterFactory.CreateChannelAdapter();
        IZLinkBackendPublisherSocket? publisher = null;
        ZLinkChannelRuntimeBundle? bundle = null;
        try
        {
            publisher = adapter.CreatePublisherSocket(state.Context);
            publisher.SetChannelName(channelName);
            ApplySocketConfig(publisher, channel.Publisher!.SocketConfig);
            RoutingId localRid = default;
            if (channel.RoutingId.Size > 0)
            {
                localRid = ZLinkRoutingIdPolicy.Derive(channel.RoutingId, "pub");
                publisher.SetRoutingId(localRid);
            }

            publisher.Bind(channel.Publisher!.BindEndpoint!);
            bundle = new ZLinkChannelRuntimeBundle(
                socket: publisher,
                submitter: new ZLinkAsyncSubmitter(
                    publisher.OnSendReady,
                    channel.Publisher.SocketConfig.SendTimeout ?? registration.DefaultSocketSendTimeout,
                    state.StopTokenSource.Token),
                localRid: localRid,
                socketRole: "pub");

            return bundle;
        }
        catch
        {
            if (bundle is not null)
                await bundle.DisposeAsync().ConfigureAwait(false);
            else if (publisher is not null)
                await publisher.DisposeAsync().ConfigureAwait(false);

            throw;
        }
    }

    internal static void ApplySocketConfig(
        IZLinkBackendSocketOptions socket,
        IZLinkSocketConfig config)
    {
        if (config.MaxMessageSize > 0) socket.SetMaxMessageSize(config.MaxMessageSize);

        if (config.SendHighWaterMark > 0) socket.SetSendHighWaterMark(config.SendHighWaterMark);

        if (config.ReceiveHighWaterMark > 0) socket.SetReceiveHighWaterMark(config.ReceiveHighWaterMark);
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

}
