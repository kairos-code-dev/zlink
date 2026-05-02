using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelRuntimeManager(
    IServiceProvider services,
    IZLinkBackendAdapterFactory backendAdapterFactory,
    ZLinkFrameworkRegistration registration,
    ZLinkChannelMessagePump channelMessagePump)
{
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
                throw new InvalidOperationException($"Channel client '{channelName}' is not registered.");
            }

            var adapter = backendAdapterFactory.CreateChannelAdapter();
            var dealer = adapter.CreateDealerSocket(state.Context);
            dealer.SetChannelName(channelName);
            if (!string.IsNullOrWhiteSpace(channel.Client.BindEndpoint))
            {
                dealer.Bind(channel.Client.BindEndpoint);
            }

            var bundle = new ZLinkChannelRuntimeBundle(
                dealer,
                new ZLinkAsyncSubmitter(
                    dealer.OnSendReady,
                    channel.Client.SocketOptions.SendTimeout,
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
                    var discovery = CreateDiscovery(
                        adapter,
                        state,
                        channelName,
                        ResolveClientAutoConnectType(channel),
                        registration.Discovery?.Endpoints ?? []);
                    dealer.AttachDiscovery(discovery);
                    bundle.Discovery = discovery;
                }

                state.ClientBundles.Add(channelName, bundle);
                return bundle;
            }
            catch
            {
                _ = Task.Run(async () => await bundle.DisposeAsync().ConfigureAwait(false));
                throw;
            }
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
                throw new InvalidOperationException($"Channel publisher '{channelName}' is not registered.");
            }

            var bundle = CreatePublisherBundle(state, channelName, channel);
            state.PublisherBundles.Add(channelName, bundle);
            return bundle;
        }
    }

    public ZLinkRoutedChannelRuntime GetRoutedChannel(
        ZLinkFrameworkRuntimeState state,
        string routerChannelId)
    {
        return state.RoutedChannels.TryGetValue(routerChannelId, out var routed)
            ? routed
            : throw new InvalidOperationException($"Routed channel '{routerChannelId}' is not registered.");
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
                var bundle = CreateServerBundle(state, adapter, channelName, channel);
                state.ServerBundles.Add(channelName, bundle);
                state.ListenerTasks.Add(Task.Run(
                    () => channelMessagePump.RunServerLoopAsync(
                        channelName,
                        (IZLinkBackendRouterSocket)bundle.Socket,
                        state.StopTokenSource.Token),
                    state.StopTokenSource.Token));
            }

            if (channel.Subscriber is not null)
            {
                var bundle = CreateSubscriberBundle(state, adapter, channelName, channel);
                state.SubscriberBundles.Add(channelName, bundle);
                state.ListenerTasks.Add(Task.Run(
                    () => channelMessagePump.RunSubscriberLoopAsync(
                        channelName,
                        (IZLinkBackendSubscriberSocket)bundle.Socket,
                        state.StopTokenSource.Token),
                    state.StopTokenSource.Token));
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

            state.PublisherBundles.Add(entry.Key, CreatePublisherBundle(state, entry.Key, entry.Value, adapter));
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

    public void InitializeRoutedChannels(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter)
    {
        foreach (var routedRegistration in registration.RoutedChannels.Values)
        {
            var router = adapter.CreateRouterSocket(state.Context);
            router.SetChannelName(routedRegistration.RouterChannelId);
            if (routedRegistration.RoutingOptions.RoutingId.Size > 0)
            {
                router.SetRoutingId(routedRegistration.RoutingOptions.RoutingId);
            }

            router.Bind(routedRegistration.BindEndpoint!);
            IZLinkBackendDiscovery? discovery = null;
            if (routedRegistration.ManualConnections.Count == 0
                && registration.Discovery?.Endpoints.Count > 0)
            {
                discovery = CreateDiscovery(
                    adapter,
                    state,
                    routedRegistration.RouterChannelId,
                    ZLinkAutoConnectType.RouteMesh,
                    registration.Discovery.Endpoints);
                router.AttachDiscovery(discovery);
            }

            var handlers = new ZLinkRoutedHandlerRegistry(CreateRoutedHandlerDescriptors(routedRegistration));
            var runtime = new ZLinkRoutedChannelRuntime(
                services,
                routedRegistration,
                router,
                discovery,
                handlers,
                new ZLinkSessionActorDispatchRoutedPacketDispatcher(services),
                state.StopTokenSource.Token);
            foreach (var endpoint in routedRegistration.ManualConnections)
            {
                runtime.Connect(endpoint);
            }

            runtime.Start();
            state.RoutedChannels.Add(routedRegistration.RouterChannelId, runtime);
        }
    }

    public IZLinkEndpointConnections GetClientConnections(
        ZLinkFrameworkRuntimeState state,
        string channelName)
    {
        if (!registration.Channels.TryGetValue(channelName, out var channel)
            || channel.Client is null)
        {
            throw new InvalidOperationException($"Channel client '{channelName}' is not registered.");
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
            throw new InvalidOperationException($"Channel subscriber '{channelName}' is not registered.");
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

    private ZLinkChannelRuntimeBundle CreateServerBundle(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter,
        string channelName,
        ZLinkChannelRegistration channel)
    {
        var router = adapter.CreateRouterSocket(state.Context);
        router.SetChannelName(channelName);
        router.Bind(channel.Server!.BindEndpoint!);
        var bundle = new ZLinkChannelRuntimeBundle(router);

        if (registration.Discovery is not null)
        {
            var discovery = CreateDiscovery(
                adapter,
                state,
                channelName,
                ZLinkAutoConnectType.ClientServer,
                registration.Discovery.Endpoints);
            router.AttachDiscovery(discovery);
            bundle.Discovery = discovery;
        }

        return bundle;
    }

    private ZLinkChannelRuntimeBundle CreateSubscriberBundle(
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
            var discovery = CreateDiscovery(
                adapter,
                state,
                channelName,
                ZLinkAutoConnectType.Fanout,
                registration.Discovery?.Endpoints ?? []);
            subscriber.AttachDiscovery(discovery);
            bundle.Discovery = discovery;
        }

        return bundle;
    }

    private ZLinkChannelRuntimeBundle CreatePublisherBundle(
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
                channel.Publisher.SocketOptions.SendTimeout,
                state.StopTokenSource.Token));

        if (registration.Discovery is not null)
        {
            var discovery = CreateDiscovery(
                adapter,
                state,
                channelName,
                ZLinkAutoConnectType.Fanout,
                registration.Discovery.Endpoints);
            publisher.AttachDiscovery(discovery);
            bundle.Discovery = discovery;
        }

        return bundle;
    }

    private static IZLinkBackendDiscovery CreateDiscovery(
        IZLinkChannelBackendAdapter adapter,
        ZLinkFrameworkRuntimeState state,
        string channelName,
        ZLinkAutoConnectType autoConnectType,
        IReadOnlyCollection<string> endpoints)
    {
        var discovery = adapter.CreateDiscovery(state.Context, autoConnectType, channelName);
        foreach (var endpoint in endpoints)
        {
            discovery.ConnectRegistry(endpoint);
        }

        return discovery;
    }

    private static ZLinkAutoConnectType ResolveClientAutoConnectType(ZLinkChannelRegistration channel)
    {
        return channel.AutoConnectType == ZLinkAutoConnectType.DealerMesh
            ? ZLinkAutoConnectType.DealerMesh
            : ZLinkAutoConnectType.ClientServer;
    }

    private static IEnumerable<ZLinkRoutedHandlerDescriptor> CreateRoutedHandlerDescriptors(
        ZLinkRoutedChannelRegistration routedRegistration)
    {
        foreach (var handler in routedRegistration.SendHandlers)
        {
            yield return new ZLinkRoutedHandlerDescriptor(
                ZLinkMessageKind.Command,
                routedRegistration.RouterChannelId,
                handler.PacketName ?? ZLinkMessageNameResolver.ResolveFromType(handler.MessageType),
                handler.HandlerType,
                handler.MessageType,
                null,
                handler.HandlerType.GetMethod(nameof(IZLinkRoutedSendHandler<object>.HandleAsync))!);
        }

        foreach (var handler in routedRegistration.RequestHandlers)
        {
            yield return new ZLinkRoutedHandlerDescriptor(
                ZLinkMessageKind.Request,
                routedRegistration.RouterChannelId,
                handler.PacketName ?? ZLinkMessageNameResolver.ResolveFromType(handler.MessageType),
                handler.HandlerType,
                handler.MessageType,
                handler.ReplyType,
                handler.HandlerType.GetMethod(nameof(IZLinkRoutedRequestHandler<object, object>.HandleAsync))!);
        }
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
