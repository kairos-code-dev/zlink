using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Core;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelRuntimeManager(
    IServiceProvider services,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration,
    ZLinkChannelMessagePump channelMessagePump)
{
    private readonly ZLinkChannelBundleFactory _bundleFactory = new(backendAdapterFactory, registration);
    private readonly ZLinkRouteChannelInitializer _routeChannels = new(services, registration);

    public ZLinkChannelRuntimeBundle GetOrCreateClientBundle(
        ZLinkFrameworkRuntimeState state,
        string channelName)
    {
        lock (state.SyncRoot)
        {
            if (state.ClientBundles.TryGetValue(channelName, out var existing))
            {
                return existing;
            }

            if (!registration.Channels.TryGetValue(channelName, out var channel)
                || channel.Client is null)
            {
                throw new ZLinkConfigurationException($"Channel client '{channelName}' is not registered.");
            }

            var bundle = _bundleFactory.CreateClientBundle(state, channelName, channel);
            state.ClientBundles.Add(channelName, bundle);
            return bundle;
        }
    }

    public ZLinkChannelRuntimeBundle GetOrCreatePublisherBundle(
        ZLinkFrameworkRuntimeState state,
        string channelName)
    {
        lock (state.SyncRoot)
        {
            if (state.PublisherBundles.TryGetValue(channelName, out var existing))
            {
                return existing;
            }

            if (!registration.Channels.TryGetValue(channelName, out var channel)
                || channel.Publisher is null)
            {
                throw new ZLinkConfigurationException($"Channel publisher '{channelName}' is not registered.");
            }

            var bundle = _bundleFactory.CreatePublisherBundle(state, channelName, channel);
            state.PublisherBundles.Add(channelName, bundle);
            return bundle;
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

    public void InitializeInboundChannels(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter)
    {
        foreach (var entry in registration.Channels)
        {
            var channelName = entry.Key;
            var channel = entry.Value;

            if (channel.Server is not null)
            {
                var bundle = _bundleFactory.CreateServerBundle(state, adapter, channelName, channel);
                state.ServerBundles.Add(channelName, bundle);
                state.ListenerTasks.Add(state.TaskRunner.Run(
                    $"channel-server:{channelName}",
                    ct => new ValueTask(channelMessagePump.RunServerLoopAsync(
                        channelName,
                        (IZLinkBackendRouterSocket)bundle.Socket,
                        ct))));
            }

            if (channel.Subscriber is not null)
            {
                var bundle = _bundleFactory.CreateSubscriberBundle(state, adapter, channelName, channel);
                state.SubscriberBundles.Add(channelName, bundle);
                state.ListenerTasks.Add(state.TaskRunner.Run(
                    $"channel-subscriber:{channelName}",
                    ct => new ValueTask(channelMessagePump.RunSubscriberLoopAsync(
                        channelName,
                        (IZLinkBackendSubscriberSocket)bundle.Socket,
                        ct))));
            }
        }
    }

    public void InitializePublisherChannels(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter)
    {
        foreach (var entry in registration.Channels)
        {
            if (entry.Value.Publisher is null)
            {
                continue;
            }

            state.PublisherBundles.Add(
                entry.Key,
                _bundleFactory.CreatePublisherBundle(state, entry.Key, entry.Value, adapter));
        }
    }

    public void InitializeClientChannels(ZLinkFrameworkRuntimeState state)
    {
        foreach (var entry in registration.Channels)
        {
            if (entry.Value.Client is not null)
            {
                _ = GetOrCreateClientBundle(state, entry.Key);
            }
        }
    }

    public void InitializeRouteChannels(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter)
    {
        _routeChannels.Initialize(state, adapter);
    }

    public IZLinkEndpointConnections GetClientConnections(
        ZLinkFrameworkRuntimeState state,
        string channelName)
    {
        if (!registration.Channels.TryGetValue(channelName, out var channel)
            || channel.Client is null)
        {
            throw new ZLinkConfigurationException($"Channel client '{channelName}' is not registered.");
        }

        return new ZLinkRuntimeConnections(
            (endpoint, _) =>
            {
                var bundle = GetOrCreateClientBundle(state, channelName);
                if (!bundle.TryAddManualConnection(endpoint))
                {
                    return ValueTask.FromResult(false);
                }

                ((IZLinkBackendDealerSocket)bundle.Socket).Connect(endpoint);
                return ValueTask.FromResult(true);
            },
            (endpoint, _) =>
            {
                var bundle = GetOrCreateClientBundle(state, channelName);
                ((IZLinkBackendDealerSocket)bundle.Socket).Disconnect(endpoint);
                bundle.RemoveManualConnection(endpoint);
                return ValueTask.CompletedTask;
            },
            _ => ValueTask.FromResult<IReadOnlyList<string>>(
                GetOrCreateClientBundle(state, channelName).ListManualConnections()));
    }

    public IZLinkEndpointConnections GetSubscriberConnections(
        ZLinkFrameworkRuntimeState state,
        string channelName)
    {
        if (!state.SubscriberBundles.TryGetValue(channelName, out var bundle))
        {
            throw new ZLinkConfigurationException($"Channel subscriber '{channelName}' is not registered.");
        }

        return new ZLinkRuntimeConnections(
            (endpoint, _) =>
            {
                if (!bundle.TryAddManualConnection(endpoint))
                {
                    return ValueTask.FromResult(false);
                }

                ((IZLinkBackendSubscriberSocket)bundle.Socket).Connect(endpoint);
                return ValueTask.FromResult(true);
            },
            (endpoint, _) =>
            {
                ((IZLinkBackendSubscriberSocket)bundle.Socket).Disconnect(endpoint);
                bundle.RemoveManualConnection(endpoint);
                return ValueTask.CompletedTask;
            },
            _ => ValueTask.FromResult<IReadOnlyList<string>>(bundle.ListManualConnections()));
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
            "publisher" => GetOrCreatePublisherBundle(state, channelName).Socket,
            "client" => GetOrCreateClientBundle(state, channelName).Socket,
            _ => throw new InvalidOperationException(
                $"Socket monitoring source '{sourceName}' is not registered."),
        };
    }

    private static (string ChannelName, string Capability) ParseChannelCapabilitySource(string sourceName)
    {
        ArgumentException.ThrowIfNullOrEmpty(sourceName);

        var separatorIndex = sourceName.LastIndexOf('.');
        if (separatorIndex <= 0 || separatorIndex == sourceName.Length - 1)
        {
            throw new InvalidOperationException(
                $"Socket monitoring source '{sourceName}' must use '<channel>.<capability>'.");
        }

        return (sourceName[..separatorIndex], sourceName[(separatorIndex + 1)..]);
    }
}
