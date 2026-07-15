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
            ApplyClientRoutingConfig(dealer, channel.Client.RoutingConfig);
            if (!string.IsNullOrWhiteSpace(channel.Client.BindEndpoint)) dealer.Bind(channel.Client.BindEndpoint);

            bundle = new ZLinkChannelRuntimeBundle(
                socket: dealer,
                submitter: new ZLinkAsyncSubmitter(
                    dealer.OnSendReady,
                    channel.Client.SocketConfig.SendTimeout ?? registration.DefaultSocketSendTimeout,
                    state.StopTokenSource.Token),
                localRid: localRid,
                socketRole: "dealer");

            if (channel.Client.AcquisitionMode == ZLinkPeerAcquisitionMode.Manual)
                channel.Client.ManualConnections.Attach(
                    endpoint => bundle.ConnectManual(dealer, endpoint),
                    endpoint => bundle.DisconnectManual(dealer, endpoint));

            return bundle;
        }
        catch (Exception initializationFailure)
        {
            await ThrowAfterCleanupAsync(initializationFailure, bundle, dealer).ConfigureAwait(false);
            throw new InvalidOperationException("Unreachable after startup cleanup failure propagation.");
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
            ApplyServerRoutingConfig(router, channel.Server.RoutingConfig);
            router.Bind(channel.Server!.BindEndpoint!);
            bundle = new ZLinkChannelRuntimeBundle(router, localRid: localRid, socketRole: "router");

            return bundle;
        }
        catch (Exception initializationFailure)
        {
            await ThrowAfterCleanupAsync(initializationFailure, bundle, router).ConfigureAwait(false);
            throw new InvalidOperationException("Unreachable after startup cleanup failure propagation.");
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
                // SUB has no addressable routing-id socket option. Keep the allocated identity
                // in the framework bundle for tracking and diagnostics; it is not a publish
                // destination and therefore is not written to the native socket.
            }

            subscriber.SetSubscription(string.Empty);
            bundle = new ZLinkChannelRuntimeBundle(subscriber, localRid: localRid, socketRole: "sub");

            if (channel.Subscriber.AcquisitionMode == ZLinkPeerAcquisitionMode.Manual)
                channel.Subscriber.ManualConnections.Attach(
                    endpoint => bundle.ConnectManual(subscriber, endpoint),
                    endpoint => bundle.DisconnectManual(subscriber, endpoint));

            return bundle;
        }
        catch (Exception initializationFailure)
        {
            await ThrowAfterCleanupAsync(initializationFailure, bundle, subscriber).ConfigureAwait(false);
            throw new InvalidOperationException("Unreachable after startup cleanup failure propagation.");
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
                // PUB has no addressable routing-id socket option. The framework retains this
                // identity for packet tracking and diagnostics only.
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
        catch (Exception initializationFailure)
        {
            await ThrowAfterCleanupAsync(initializationFailure, bundle, publisher).ConfigureAwait(false);
            throw new InvalidOperationException("Unreachable after startup cleanup failure propagation.");
        }
    }

    internal static void ApplySocketConfig(
        IZLinkBackendSocketOptions socket,
        IZLinkSocketConfig config)
    {
        socket.ApplySocketConfig(config);
    }

    internal static void ApplyServerRoutingConfig(
        IZLinkBackendRouterSocket socket,
        IZLinkRouteConfig config)
    {
        socket.SetMandatory(config.RequireKnownPeer);
        socket.SetHandover(config.AllowPeerHandover);
        socket.SetProbe(config.EnablePeerProbe);
        if (config.ConnectRoutingId.Size > 0) socket.SetConnectRoutingId(config.ConnectRoutingId);
    }

    internal static void ApplyClientRoutingConfig(
        IZLinkBackendDealerSocket socket,
        IZLinkOutboundRouteConfig config)
    {
        socket.SetProbe(config.ProbeRouterOnConnect);
    }

    private static async ValueTask ThrowAfterCleanupAsync(
        Exception initializationFailure,
        IAsyncDisposable? composite,
        IAsyncDisposable? standalone)
    {
        var failures = new ZLinkFailureCollector(initializationFailure);
        if (composite is not null)
            await failures.CaptureAsync(composite.DisposeAsync).ConfigureAwait(false);
        else if (standalone is not null)
            await failures.CaptureAsync(standalone.DisposeAsync).ConfigureAwait(false);
        failures.ThrowIfAny();
    }

}
