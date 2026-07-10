namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelRuntimeManager(
    IServiceProvider services,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration,
    ZLinkChannelReceiveLoop receiveLoop)
{
    private readonly ZLinkChannelBundleFactory _bundleFactory = new(backendAdapterFactory, registration);
    private readonly ZLinkRouteChannelInitializer _routeChannels = new(services, registration);

    public ZLinkChannelRuntimeBundle GetClientBundle(
        ZLinkFrameworkRuntimeState state,
        string channelName)
    {
        lock (state.SyncRoot)
        {
            if (state.ClientBundles.TryGetValue(channelName, out var existing)) return existing;
            if (!registration.Channels.TryGetValue(channelName, out var channel)
                || channel.Client is null)
                throw new ZLinkConfigurationException($"Channel client '{channelName}' is not registered.");

            throw new ZLinkConfigurationException($"Channel client '{channelName}' is not initialized.");
        }
    }

    public ZLinkChannelRuntimeBundle GetPublisherBundle(
        ZLinkFrameworkRuntimeState state,
        string channelName)
    {
        lock (state.SyncRoot)
        {
            if (state.PublisherBundles.TryGetValue(channelName, out var existing)) return existing;
            if (!registration.Channels.TryGetValue(channelName, out var channel)
                || channel.Publisher is null)
                throw new ZLinkConfigurationException($"Channel publisher '{channelName}' is not registered.");

            throw new ZLinkConfigurationException($"Channel publisher '{channelName}' is not initialized.");
        }
    }

    public ZLinkRouteChannelRuntime GetRouteChannel(
        ZLinkFrameworkRuntimeState state,
        string routerChannelId)
    {
        return state.RouteChannels.TryGetValue(routerChannelId, out var routed)
            ? routed
            : throw new ZLinkConfigurationException($"Route channel '{routerChannelId}' is not registered.");
    }

    public async ValueTask InitializeInboundChannelsAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter)
    {
        foreach (var entry in registration.Channels)
        {
            var channelName = entry.Key;
            var channel = entry.Value;

            if (channel.Server is not null)
            {
                var bundle = await _bundleFactory.CreateServerBundleAsync(state, adapter, channelName, channel)
                    .ConfigureAwait(false);
                state.ServerBundles.Add(channelName, bundle);
                state.ListenerTasks.Add(state.TaskRunner.Run(
                    $"channel-server:{channelName}",
                    ct => new ValueTask(receiveLoop.RunServerLoopAsync(
                        channelName,
                        (IZLinkBackendRouterSocket)bundle.Socket,
                        bundle.ReceiveGate,
                        ct))));
            }

            if (channel.Subscriber is not null)
            {
                var bundle = await _bundleFactory.CreateSubscriberBundleAsync(state, adapter, channelName, channel)
                    .ConfigureAwait(false);
                state.SubscriberBundles.Add(channelName, bundle);
                state.ListenerTasks.Add(state.TaskRunner.Run(
                    $"channel-subscriber:{channelName}",
                    ct => new ValueTask(receiveLoop.RunSubscriberLoopAsync(
                        channelName,
                        (IZLinkBackendSubscriberSocket)bundle.Socket,
                        ct))));
            }
        }
    }

    public async ValueTask InitializePublisherChannelsAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter)
    {
        foreach (var entry in registration.Channels)
        {
            if (entry.Value.Publisher is null) continue;

            state.PublisherBundles.Add(
                entry.Key,
                await _bundleFactory.CreatePublisherBundleAsync(state, entry.Key, entry.Value, adapter)
                    .ConfigureAwait(false));
        }
    }

    public async ValueTask InitializeClientChannelsAsync(ZLinkFrameworkRuntimeState state)
    {
        foreach (var entry in registration.Channels)
            if (entry.Value.Client is not null)
                state.ClientBundles.Add(
                    entry.Key,
                    await _bundleFactory.CreateClientBundleAsync(state, entry.Key, entry.Value)
                        .ConfigureAwait(false));
    }

    public ValueTask InitializeRouteChannelsAsync(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter)
    {
        return _routeChannels.InitializeAsync(state, adapter);
    }

    public IZLinkBackendSocket GetMonitoringSocket(
        ZLinkFrameworkRuntimeState state,
        string sourceName)
    {
        var (channelName, capability) = ParseChannelCapabilitySource(sourceName);

        return capability switch
        {
            "server" => state.ServerBundles.TryGetValue(channelName, out var serverBundle)
                ? serverBundle.Socket
                : throw new InvalidOperationException(
                    $"Socket monitoring source '{sourceName}' is not registered."),
            "subscriber" => state.SubscriberBundles.TryGetValue(channelName, out var subscriberBundle)
                ? subscriberBundle.Socket
                : throw new InvalidOperationException(
                    $"Socket monitoring source '{sourceName}' is not registered."),
            "publisher" => GetPublisherBundle(state, channelName).Socket,
            "client" => GetClientBundle(state, channelName).Socket,
            _ => throw new InvalidOperationException(
                $"Socket monitoring source '{sourceName}' is not registered.")
        };
    }

    private static (string ChannelName, string Capability) ParseChannelCapabilitySource(string sourceName)
    {
        ArgumentException.ThrowIfNullOrEmpty(sourceName);

        var separatorIndex = sourceName.LastIndexOf('.');
        if (separatorIndex <= 0 || separatorIndex == sourceName.Length - 1)
            throw new InvalidOperationException(
                $"Socket monitoring source '{sourceName}' must use '<channel>.<capability>'.");

        return (sourceName[..separatorIndex], sourceName[(separatorIndex + 1)..]);
    }
}
