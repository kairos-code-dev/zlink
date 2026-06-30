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

            // weight 는 bind/connect/discovery 前에 적용해 default-weight 노출 창을 없앤다.
            dealer.SetPeerWeight(channel.Client.SocketConfig.Weight);
            if (!string.IsNullOrWhiteSpace(channel.Client.BindEndpoint)) dealer.Bind(channel.Client.BindEndpoint);

            bundle = new ZLinkChannelRuntimeBundle(
                dealer,
                new ZLinkAsyncSubmitter(
                    dealer.OnSendReady,
                    channel.Client.SocketConfig.SendTimeout ?? registration.DefaultSocketSendTimeout,
                    state.StopTokenSource.Token),
                localRid,
                "dealer");

            if (channel.Client.ManualConnections.Count > 0)
                AttachManualConnections(bundle, dealer, channel.Client.ManualConnections);
            else
                AttachDiscovery(
                    bundle,
                    adapter,
                    state.Context,
                    channelName,
                    ZLinkAutoConnectType.ClientServer,
                    registration.Discovery?.Endpoints ?? [],
                    dealer.AttachDiscovery);

            return bundle;
        }
        catch
        {
            if (bundle is not null)
                DisposeFailedBundle(bundle);
            else if (dealer is not null) dealer.DisposeAsync().AsTask().GetAwaiter().GetResult();

            throw;
        }
    }

    public ZLinkChannelRuntimeBundle CreateServerBundle(
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

            if (registration.Discovery is not null)
                AttachDiscovery(
                    bundle,
                    adapter,
                    state.Context,
                    channelName,
                    ZLinkAutoConnectType.ClientServer,
                    registration.Discovery.Endpoints,
                    router.AttachDiscovery);

            return bundle;
        }
        catch
        {
            if (bundle is not null)
                DisposeFailedBundle(bundle);
            else if (router is not null) router.DisposeAsync().AsTask().GetAwaiter().GetResult();

            throw;
        }
    }

    public ZLinkChannelRuntimeBundle CreateSubscriberBundle(
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

            if (channel.Subscriber!.ManualConnections.Count > 0)
                AttachManualConnections(bundle, subscriber, channel.Subscriber.ManualConnections);
            else
                AttachDiscovery(
                    bundle,
                    adapter,
                    state.Context,
                    channelName,
                    ZLinkAutoConnectType.Fanout,
                    registration.Discovery?.Endpoints ?? [],
                    subscriber.AttachDiscovery);

            return bundle;
        }
        catch
        {
            if (bundle is not null)
                DisposeFailedBundle(bundle);
            else if (subscriber is not null) subscriber.DisposeAsync().AsTask().GetAwaiter().GetResult();

            throw;
        }
    }

    public ZLinkChannelRuntimeBundle CreatePublisherBundle(
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
                publisher,
                new ZLinkAsyncSubmitter(
                    publisher.OnSendReady,
                    channel.Publisher.SocketConfig.SendTimeout ?? registration.DefaultSocketSendTimeout,
                    state.StopTokenSource.Token),
                localRid,
                "pub");

            if (registration.Discovery is not null)
                AttachDiscovery(
                    bundle,
                    adapter,
                    state.Context,
                    channelName,
                    ZLinkAutoConnectType.Fanout,
                    registration.Discovery.Endpoints,
                    publisher.AttachDiscovery);

            return bundle;
        }
        catch
        {
            if (bundle is not null)
                DisposeFailedBundle(bundle);
            else if (publisher is not null) publisher.DisposeAsync().AsTask().GetAwaiter().GetResult();

            throw;
        }
    }

    internal static void ApplySocketConfig(
        IZLinkBackendSocketOptions socket,
        IZLinkSocketConfig config)
    {
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

    private static void DisposeFailedBundle(ZLinkChannelRuntimeBundle bundle)
    {
        bundle.DisposeAsync().AsTask().GetAwaiter().GetResult();
    }
}