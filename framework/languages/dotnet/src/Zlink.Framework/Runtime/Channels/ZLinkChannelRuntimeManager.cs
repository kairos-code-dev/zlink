namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelRuntimeManager(
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration,
    ZLinkChannelReceiveLoop receiveLoop)
{
    private readonly ZLinkChannelBundleFactory _bundleFactory = new(backendAdapterFactory, registration);

    public ZLinkChannelRuntimeBundle GetPublisherBundle(
        ZLinkFrameworkComponentState state,
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

    public async ValueTask InitializeInboundChannelsAsync(
        ZLinkFrameworkComponentState state,
        IZLinkChannelBackendAdapter adapter)
    {
        foreach (var entry in registration.Channels)
        {
            var channelName = entry.Key;
            var channel = entry.Value;

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
        ZLinkFrameworkComponentState state,
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

    public IZLinkBackendSocket GetMonitoringSocket(
        ZLinkFrameworkComponentState state,
        string sourceName)
    {
        var (channelName, capability) = ParseChannelCapabilitySource(sourceName);

        return capability switch
        {
            "subscriber" => state.SubscriberBundles.TryGetValue(channelName, out var subscriberBundle)
                ? subscriberBundle.Socket
                : throw new InvalidOperationException(
                    $"Socket monitoring source '{sourceName}' is not registered."),
            "publisher" => GetPublisherBundle(state, channelName).Socket,
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
