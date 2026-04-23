using Zlink.Framework.Backend;

namespace Zlink.Framework;

internal sealed class ZLinkFrameworkRuntime
{
    private readonly IServiceProvider _services;
    private readonly IZLinkBackendAdapterFactory _backendAdapterFactory;
    private readonly ZLinkFrameworkRegistration _registration;
    private readonly ZLinkRegistryRuntime? _registryRuntime;
    private readonly ZLinkHandlerRegistry _handlerRegistry;
    private readonly ZLinkHandlerDispatcher _dispatcher;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private ZLinkFrameworkRuntimeState? _state;

    public ZLinkFrameworkRuntime(
        IServiceProvider services,
        IZLinkBackendAdapterFactory backendAdapterFactory,
        ZLinkFrameworkRegistration registration,
        ZLinkHandlerRegistry handlerRegistry,
        ZLinkHandlerDispatcher dispatcher,
        ZLinkRegistryRuntime? registryRuntime = null)
    {
        _services = services;
        _backendAdapterFactory = backendAdapterFactory;
        _registration = registration;
        _handlerRegistry = handlerRegistry;
        _dispatcher = dispatcher;
        _registryRuntime = registryRuntime;
    }

    public IZLinkBackendContext? Context => _state?.Context;

    public ZLinkFrameworkRegistration Registration => _registration;

    public bool IsStarted => _state is not null;

    public async ValueTask StartAsync(CancellationToken cancellationToken)
    {
        if (_registryRuntime is not null)
        {
            await _registryRuntime.StartAsync(cancellationToken);
        }

        await _gate.WaitAsync(cancellationToken);
        try
        {
            if (_state is not null)
            {
                return;
            }

            var channelAdapter = _backendAdapterFactory.CreateChannelAdapter();
            var state = new ZLinkFrameworkRuntimeState(channelAdapter.CreateContext(), _registration);

            try
            {
                InitializeInboundChannels(state, channelAdapter);
                InitializePublisherChannels(state, channelAdapter);
                InitializeStreamNodes(state);
                InitializeSpotNodes(state);
            }
            catch
            {
                await state.DisposeAsync();
                throw;
            }

            _state = state;
        }
        finally
        {
            _gate.Release();
        }
    }

    public async ValueTask StopAsync(CancellationToken cancellationToken)
    {
        ZLinkFrameworkRuntimeState? stateToDispose;

        await _gate.WaitAsync(cancellationToken);
        try
        {
            stateToDispose = _state;
            _state = null;
        }
        finally
        {
            _gate.Release();
        }

        if (stateToDispose is not null)
        {
            await stateToDispose.DisposeAsync();
        }

        if (_registryRuntime is not null)
        {
            await _registryRuntime.StopAsync(cancellationToken);
        }
    }

    internal ZLinkChannelRuntimeBundle GetOrCreateClientBundle(string channelName)
    {
        var state = GetOrStartState();
        lock (state.SyncRoot)
        {
            if (state.ClientBundles.TryGetValue(channelName, out var existing))
            {
                return existing;
            }

            if (!_registration.Channels.TryGetValue(channelName, out var channel)
                || channel.Client is null)
            {
                throw new InvalidOperationException($"Channel client '{channelName}' is not registered.");
            }

            var adapter = _backendAdapterFactory.CreateChannelAdapter();
            var dealer = adapter.CreateDealerSocket(state.Context);
            dealer.SetChannelName(channelName);
            var bundle = new ZLinkChannelRuntimeBundle(dealer);

            try
            {
                if (channel.Client.ManualConnections.Count > 0)
                {
                    foreach (var endpoint in channel.Client.ManualConnections)
                    {
                        dealer.Connect(endpoint);
                        bundle.ManualConnections.Add(endpoint);
                    }
                }
                else
                {
                    var discovery = CreateDiscovery(
                        adapter,
                        state,
                        channelName,
                        ZLinkBackendServiceType.Socket,
                        _registration.Discovery?.Endpoints ?? []);
                    dealer.AttachDiscovery(discovery);
                    bundle.Discovery = discovery;
                }

                state.ClientBundles.Add(channelName, bundle);
                return bundle;
            }
            catch
            {
                bundle.DisposeAsync().AsTask().GetAwaiter().GetResult();
                throw;
            }
        }
    }

    internal ZLinkChannelRuntimeBundle GetOrCreatePublisherBundle(string channelName)
    {
        var state = GetOrStartState();
        lock (state.SyncRoot)
        {
            if (state.PublisherBundles.TryGetValue(channelName, out var existing))
            {
                return existing;
            }

            if (!_registration.Channels.TryGetValue(channelName, out var channel)
                || channel.Publisher is null)
            {
                throw new InvalidOperationException($"Channel publisher '{channelName}' is not registered.");
            }

            var bundle = CreatePublisherBundle(state, channelName, channel);
            state.PublisherBundles.Add(channelName, bundle);
            return bundle;
        }
    }

    internal ZLinkSpotPublisherBundle GetSpotPublisherBundle(string channelName)
    {
        var state = GetOrStartState();

        foreach (var node in state.SpotNodes.Values)
        {
            if (node.PublisherBundles.TryGetValue(channelName, out var bundle)
                || node.SpotFactories.Count >= 0 && nodeTryCreatePublisher(node, channelName, out bundle))
            {
                return bundle;
            }
        }

        throw new InvalidOperationException(
            $"SPOT publisher client '{channelName}' is not registered.");

        static bool nodeTryCreatePublisher(
            ZLinkSpotNodeRuntime node,
            string channelName,
            out ZLinkSpotPublisherBundle bundle)
        {
            try
            {
                bundle = node.GetOrCreatePublisherBundle(channelName);
                return true;
            }
            catch (InvalidOperationException)
            {
                bundle = null!;
                return false;
            }
        }
    }

    internal async ValueTask<ZLinkSpotCreateResult> CreateSpotAsync(
        string spotName,
        global::Zlink.RoutingId? spotRid,
        CancellationToken cancellationToken)
    {
        foreach (var node in GetOrStartState().SpotNodes.Values)
        {
            if (node.SpotFactories.ContainsKey(spotName))
            {
                return await node.CreateAsync(spotName, spotRid, cancellationToken);
            }
        }

        throw new InvalidOperationException($"SPOT factory '{spotName}' is not registered.");
    }

    internal async ValueTask<ZLinkSpotInfo?> GetSpotAsync(
        global::Zlink.RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        foreach (var node in GetOrStartState().SpotNodes.Values)
        {
            var info = await node.GetAsync(spotRid, cancellationToken);
            if (info is not null)
            {
                return info;
            }
        }

        return null;
    }

    internal async ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListSpotsAsync(
        CancellationToken cancellationToken)
    {
        var results = new List<ZLinkSpotInfo>();
        foreach (var node in GetOrStartState().SpotNodes.Values)
        {
            results.AddRange(await node.ListAsync(cancellationToken));
        }

        return results
            .OrderBy(static info => info.SpotName, StringComparer.Ordinal)
            .ToArray();
    }

    internal async ValueTask<bool> RemoveSpotAsync(
        global::Zlink.RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        foreach (var node in GetOrStartState().SpotNodes.Values)
        {
            if (await node.RemoveAsync(spotRid, cancellationToken))
            {
                return true;
            }
        }

        return false;
    }

    internal ISpotRouterConnections GetSpotRouterConnections(string spotNodeName)
    {
        var node = GetSpotNode(spotNodeName);
        return new ZLinkSpotRouterConnections(
            endpoint => node.ConnectRouterAsync(endpoint, CancellationToken.None).AsTask().GetAwaiter().GetResult(),
            node.DisconnectRouter,
            () => node.RouterManualConnections.AsReadOnly());
    }

    internal ISpotPubSubConnections GetSpotPubSubConnections(string spotNodeName)
    {
        var node = GetSpotNode(spotNodeName);
        return new ZLinkSpotPubSubConnections(
            endpoint => node.ConnectPubSubAsync(endpoint, CancellationToken.None).AsTask().GetAwaiter().GetResult(),
            node.DisconnectPubSub,
            () => node.PubSubManualConnections.AsReadOnly());
    }

    internal IChannelClientConnections GetSpotChannelClientConnections(
        string spotNodeName,
        string channelName)
    {
        var node = GetSpotNode(spotNodeName);
        if (!node.AttachedChannelBundles.TryGetValue(channelName, out var bundle))
        {
            bundle = node.GetOrCreateAttachedChannelBundle(channelName);
        }

        return new ZLinkRuntimeConnections(
            endpoint =>
            {
                if (bundle.ManualConnections.Contains(endpoint, StringComparer.Ordinal))
                {
                    return false;
                }

                bundle.Socket.Connect(endpoint);
                bundle.ManualConnections.Add(endpoint);
                return true;
            },
            endpoint =>
            {
                bundle.Socket.Disconnect(endpoint);
                bundle.ManualConnections.Remove(endpoint);
            },
            () => bundle.ManualConnections.AsReadOnly());
    }

    internal ISpotPublisherConnections GetSpotPublisherConnections(
        string spotNodeName,
        string channelName)
    {
        var node = GetSpotNode(spotNodeName);
        if (!node.PublisherBundles.TryGetValue(channelName, out var bundle))
        {
            bundle = node.GetOrCreatePublisherBundle(channelName);
        }

        return new ZLinkSpotPublisherConnections(
            endpoint => node.ConnectPubSubAsync(endpoint, CancellationToken.None).AsTask().GetAwaiter().GetResult(),
            node.DisconnectPubSub,
            () => node.PubSubManualConnections.AsReadOnly());
    }

    internal IChannelClientConnections GetClientConnections(string channelName)
    {
        if (!_registration.Channels.TryGetValue(channelName, out var channel)
            || channel.Client is null)
        {
            throw new InvalidOperationException($"Channel client '{channelName}' is not registered.");
        }

        return new ZLinkRuntimeConnections(
            endpoint =>
            {
                var bundle = GetOrCreateClientBundle(channelName);
                if (bundle.ManualConnections.Contains(endpoint, StringComparer.Ordinal))
                {
                    return false;
                }

                bundle.Socket.RequireNative<global::Zlink.DealerSocket>().Connect(endpoint);
                bundle.ManualConnections.Add(endpoint);
                return true;
            },
            endpoint =>
            {
                var bundle = GetOrCreateClientBundle(channelName);
                bundle.Socket.RequireNative<global::Zlink.DealerSocket>().Disconnect(endpoint);
                bundle.ManualConnections.Remove(endpoint);
            },
            () => GetOrCreateClientBundle(channelName).ManualConnections.AsReadOnly());
    }

    internal IChannelSubscriberConnections GetSubscriberConnections(string channelName)
    {
        var state = GetOrStartState();
        if (!state.SubscriberBundles.TryGetValue(channelName, out var bundle))
        {
            throw new InvalidOperationException($"Channel subscriber '{channelName}' is not registered.");
        }

        return new ZLinkRuntimeConnections(
            endpoint =>
            {
                if (bundle.ManualConnections.Contains(endpoint, StringComparer.Ordinal))
                {
                    return false;
                }

                bundle.Socket.RequireNative<global::Zlink.SubSocket>().Connect(endpoint);
                bundle.ManualConnections.Add(endpoint);
                return true;
            },
            endpoint =>
            {
                bundle.Socket.RequireNative<global::Zlink.SubSocket>().Disconnect(endpoint);
                bundle.ManualConnections.Remove(endpoint);
            },
            () => bundle.ManualConnections.AsReadOnly());
    }

    internal IZLinkBackendSocket GetMonitoringSocket(string sourceName)
    {
        var (channelName, capability) = ParseChannelCapabilitySource(sourceName);
        var state = GetOrStartState();

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
            "publisher" => GetOrCreatePublisherBundle(channelName).Socket,
            "client" => GetOrCreateClientBundle(channelName).Socket,
            _ => throw new InvalidOperationException(
                $"Socket monitoring source '{sourceName}' is not registered."),
        };
    }

    internal IZLinkBackendDiscovery GetMonitoringDiscovery(string sourceName)
    {
        if (_state is null)
        {
            StartAsync(CancellationToken.None).AsTask().GetAwaiter().GetResult();
        }

        if (sourceName.EndsWith(".discovery", StringComparison.Ordinal))
        {
            var logicalName = sourceName[..^".discovery".Length];

            if (_state!.SpotDiscoveries.TryGetValue(sourceName, out var spotDiscovery))
            {
                return new ZLinkBackendDiscoveryWrapper(spotDiscovery);
            }

            var socket = GetMonitoringSocket(logicalName);
            var capability = logicalName[(logicalName.LastIndexOf('.') + 1)..];
            var channelName = logicalName[..logicalName.LastIndexOf('.')];
            var state = GetOrStartState();
            var bundle = capability switch
            {
                "server" => state.ServerBundles.TryGetValue(channelName, out var serverBundle)
                    ? serverBundle
                    : null,
                "subscriber" => state.SubscriberBundles.TryGetValue(channelName, out var subscriberBundle)
                    ? subscriberBundle
                    : null,
                "publisher" => GetOrCreatePublisherBundle(channelName),
                "client" => GetOrCreateClientBundle(channelName),
                _ => null,
            };

            if (bundle?.Discovery is not null)
            {
                return bundle.Discovery;
            }
        }

        throw new InvalidOperationException(
            $"Discovery monitoring source '{sourceName}' is not registered.");
    }

    internal ZLinkSpotMonitoringSnapshot GetSpotMonitoringSnapshot(string spotNodeName)
    {
        return GetSpotNode(spotNodeName).GetMonitoringSnapshot();
    }

    private ZLinkFrameworkRuntimeState GetOrStartState()
    {
        if (_state is null)
        {
            StartAsync(CancellationToken.None).AsTask().GetAwaiter().GetResult();
        }

        return _state ?? throw new InvalidOperationException("ZLink framework runtime is not started.");
    }

    private void InitializeInboundChannels(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter)
    {
        foreach (var entry in _registration.Channels)
        {
            var channelName = entry.Key;
            var channel = entry.Value;

            if (channel.Server is not null)
            {
                var bundle = CreateServerBundle(state, adapter, channelName, channel);
                state.ServerBundles.Add(channelName, bundle);
                state.ListenerTasks.Add(Task.Run(
                    () => RunServerLoopAsync(
                        channelName,
                        bundle.Socket.RequireNative<global::Zlink.RouterSocket>(),
                        state.StopTokenSource.Token),
                    state.StopTokenSource.Token));
            }

            if (channel.Subscriber is not null)
            {
                var bundle = CreateSubscriberBundle(state, adapter, channelName, channel);
                state.SubscriberBundles.Add(channelName, bundle);
                state.ListenerTasks.Add(Task.Run(
                    () => RunSubscriberLoopAsync(
                        channelName,
                        bundle.Socket.RequireNative<global::Zlink.SubSocket>(),
                        state.StopTokenSource.Token),
                    state.StopTokenSource.Token));
            }
        }
    }

    private void InitializePublisherChannels(
        ZLinkFrameworkRuntimeState state,
        IZLinkChannelBackendAdapter adapter)
    {
        foreach (var entry in _registration.Channels)
        {
            if (entry.Value.Publisher is null)
            {
                continue;
            }

            state.PublisherBundles.Add(entry.Key, CreatePublisherBundle(state, entry.Key, entry.Value, adapter));
        }
    }

    private void InitializeSpotNodes(ZLinkFrameworkRuntimeState state)
    {
        if (_registration.SpotNodes.Count == 0)
        {
            return;
        }

        var context = state.Context.RequireNative<global::Zlink.Context>();

        foreach (var spotNodeRegistration in _registration.SpotNodes.Values)
        {
            var node = new global::Zlink.SpotNode(context);
            node.Bind(spotNodeRegistration.BindEndpoint!);

            var runtime = new ZLinkSpotNodeRuntime(
                _services,
                _registration,
                spotNodeRegistration,
                context,
                node,
                _registration.SpotDiscovery?.ChannelName
                    ?? throw new InvalidOperationException("SPOT discovery is not configured."));

            if (_registration.SpotDiscovery is not null
                && _registration.SpotDiscovery.Endpoints.Count > 0)
            {
                var discovery = new global::Zlink.Discovery(
                    context,
                    global::Zlink.ServiceType.Spot,
                    _registration.SpotDiscovery.ChannelName);
                foreach (var endpoint in _registration.SpotDiscovery.Endpoints)
                {
                    discovery.ConnectRegistry(endpoint);
                }

                node.AttachDiscovery(discovery);
                state.SpotDiscoveries.Add($"{spotNodeRegistration.SpotNodeName}.discovery", discovery);
            }

            foreach (var endpoint in spotNodeRegistration.Router?.ManualConnections ?? [])
            {
                if (!runtime.RouterManualConnections.Contains(endpoint, StringComparer.Ordinal))
                {
                    node.ConnectPeer(endpoint);
                    runtime.RouterManualConnections.Add(endpoint);
                }
            }

            foreach (var endpoint in spotNodeRegistration.PubSub?.ManualConnections ?? [])
            {
                if (!runtime.PubSubManualConnections.Contains(endpoint, StringComparer.Ordinal))
                {
                    node.ConnectPeer(endpoint);
                    runtime.PubSubManualConnections.Add(endpoint);
                }
            }
            state.SpotNodes.Add(spotNodeRegistration.SpotNodeName, runtime);
        }
    }

    private void InitializeStreamNodes(ZLinkFrameworkRuntimeState state)
    {
        if (_registration.StreamNodes.Count == 0)
        {
            return;
        }

        var context = state.Context.RequireNative<global::Zlink.Context>();
        foreach (var streamNodeRegistration in _registration.StreamNodes.Values)
        {
            var socket = new global::Zlink.StreamSocket(context);
            socket.Bind(streamNodeRegistration.BindEndpoint!);

            var runtime = new ZLinkStreamNodeRuntime(
                streamNodeRegistration.StreamNodeName,
                _services,
                socket,
                streamNodeRegistration.PacketSessionType,
                streamNodeRegistration.RawSessionType);
            runtime.Start();
            state.StreamNodes.Add(streamNodeRegistration.StreamNodeName, runtime);
        }
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

        if (_registration.Discovery is not null)
        {
            var discovery = CreateDiscovery(
                adapter,
                state,
                channelName,
                ZLinkBackendServiceType.Socket,
                _registration.Discovery.Endpoints);
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
                bundle.ManualConnections.Add(endpoint);
            }
        }
        else
        {
            var discovery = CreateDiscovery(
                adapter,
                state,
                channelName,
                ZLinkBackendServiceType.Socket,
                _registration.Discovery?.Endpoints ?? []);
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
        adapter ??= _backendAdapterFactory.CreateChannelAdapter();
        var publisher = adapter.CreatePublisherSocket(state.Context);
        publisher.SetChannelName(channelName);
        publisher.Bind(channel.Publisher!.BindEndpoint!);
        var bundle = new ZLinkChannelRuntimeBundle(publisher);

        if (_registration.Discovery is not null)
        {
            var discovery = CreateDiscovery(
                adapter,
                state,
                channelName,
                ZLinkBackendServiceType.Socket,
                _registration.Discovery.Endpoints);
            publisher.AttachDiscovery(discovery);
            bundle.Discovery = discovery;
        }

        return bundle;
    }

    private IZLinkBackendDiscovery CreateDiscovery(
        IZLinkChannelBackendAdapter adapter,
        ZLinkFrameworkRuntimeState state,
        string channelName,
        ZLinkBackendServiceType serviceType,
        IReadOnlyCollection<string> endpoints)
    {
        var discovery = adapter.CreateDiscovery(state.Context, serviceType, channelName);
        foreach (var endpoint in endpoints)
        {
            discovery.ConnectRegistry(endpoint);
        }

        return discovery;
    }

    private static (string ChannelName, string Capability) ParseChannelCapabilitySource(string sourceName)
    {
        ArgumentException.ThrowIfNullOrEmpty(sourceName);

        var separatorIndex = sourceName.LastIndexOf('.');
        if (separatorIndex <= 0 || separatorIndex == sourceName.Length - 1)
        {
            throw new InvalidOperationException(
                $"Monitoring source '{sourceName}' must use '<channel>.<capability>' form.");
        }

        return (sourceName[..separatorIndex], sourceName[(separatorIndex + 1)..]);
    }

    private ZLinkSpotNodeRuntime GetSpotNode(string spotNodeName)
    {
        var state = GetOrStartState();
        if (state.SpotNodes.TryGetValue(spotNodeName, out var node))
        {
            return node;
        }

        throw new InvalidOperationException($"SPOT node '{spotNodeName}' is not registered.");
    }

    private async Task RunServerLoopAsync(
        string channelName,
        global::Zlink.RouterSocket router,
        CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            global::Zlink.Received? received = null;
            try
            {
                received = router.Recv();
                if (received is null)
                {
                    continue;
                }

                await DispatchServerMessageAsync(channelName, router, received, cancellationToken);
            }
            catch (Exception) when (cancellationToken.IsCancellationRequested)
            {
                break;
            }
            catch (ObjectDisposedException)
            {
                break;
            }
            finally
            {
                received?.Dispose();
            }
        }
    }

    private async Task RunSubscriberLoopAsync(
        string channelName,
        global::Zlink.SubSocket subscriber,
        CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            global::Zlink.TopicMessage? topicMessage = null;
            try
            {
                topicMessage = subscriber.Subscribe();
                if (topicMessage is null)
                {
                    continue;
                }

                await DispatchEventMessageAsync(channelName, topicMessage, cancellationToken);
            }
            catch (Exception) when (cancellationToken.IsCancellationRequested)
            {
                break;
            }
            catch (ObjectDisposedException)
            {
                break;
            }
            finally
            {
                topicMessage?.Dispose();
            }
        }
    }

    private async Task DispatchServerMessageAsync(
        string channelName,
        global::Zlink.RouterSocket router,
        global::Zlink.Received received,
        CancellationToken cancellationToken)
    {
        if (received.Count == 0)
        {
            return;
        }

        var header = ZLinkEnvelopeCodec.DecodeHeader(received[0]);

        switch (header.Kind)
        {
            case ZLinkMessageKind.Request:
                await HandleRequestAsync(channelName, router, received, header, cancellationToken);
                break;
            case ZLinkMessageKind.Command:
                await HandleCommandAsync(channelName, received[0], header, cancellationToken);
                break;
        }
    }

    private async Task HandleRequestAsync(
        string channelName,
        global::Zlink.RouterSocket router,
        global::Zlink.Received received,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        var endpoint = _handlerRegistry.GetRequest(header.PacketName);
        var message = ZLinkEnvelopeCodec.DecodeBody(received[0], endpoint.MessageType);
        var context = new ZLinkRequestContext(
            channelName,
            header.PacketName,
            header.ContentType,
            header.CorrelationId,
            header.Deadline,
            EmptyServiceProvider.Instance,
            cancellationToken);

        try
        {
            var reply = await _dispatcher.DispatchAsync(endpoint, message, context, cancellationToken);
            var replyHeader = new ZLinkEnvelopeHeader(
                ZLinkMessageKind.Response,
                channelName,
                header.PacketName,
                ZLinkEnvelopeCodec.DefaultContentType,
                header.CorrelationId,
                null,
                null,
                null,
                null);
            using var replyMessage = ZLinkEnvelopeCodec.Encode(replyHeader, reply, endpoint.ReplyType);
            var routingId = received.RoutingId
                ?? throw new InvalidOperationException("Request reply requires a routing id.");
            router.Reply(routingId, received.RequestSeq ?? 0UL, replyMessage);
        }
        catch (Exception ex)
        {
            var errorHeader = new ZLinkEnvelopeHeader(
                ZLinkMessageKind.Error,
                channelName,
                header.PacketName,
                ZLinkEnvelopeCodec.DefaultContentType,
                header.CorrelationId,
                null,
                null,
                ex.GetType().Name,
                ex.Message);
            using var replyMessage = ZLinkEnvelopeCodec.Encode(errorHeader, null, null);
            var routingId = received.RoutingId
                ?? throw new InvalidOperationException("Error reply requires a routing id.");
            router.Reply(routingId, received.RequestSeq ?? 0UL, replyMessage);
        }
    }

    private async Task HandleCommandAsync(
        string channelName,
        global::Zlink.Message envelope,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        var endpoint = _handlerRegistry.GetCommand(header.PacketName);
        var message = ZLinkEnvelopeCodec.DecodeBody(envelope, endpoint.MessageType);
        var context = new ZLinkSendContext(
            channelName,
            header.PacketName,
            header.ContentType,
            header.CorrelationId,
            header.Deadline,
            EmptyServiceProvider.Instance,
            cancellationToken);
        await _dispatcher.DispatchAsync(endpoint, message, context, cancellationToken);
    }

    private async Task DispatchEventMessageAsync(
        string channelName,
        global::Zlink.TopicMessage topicMessage,
        CancellationToken cancellationToken)
    {
        if (topicMessage.Parts.Count == 0)
        {
            return;
        }

        var header = ZLinkEnvelopeCodec.DecodeHeader(topicMessage.Parts[0]);
        var endpoints = _handlerRegistry.GetEvents(header.PacketName);

        foreach (var endpoint in endpoints)
        {
            var message = ZLinkEnvelopeCodec.DecodeBody(topicMessage.Parts[0], endpoint.MessageType);
            var context = new ZLinkEventContext(
                channelName,
                header.PacketName,
                header.ContentType,
                header.CorrelationId,
                header.Deadline,
                topicMessage.Topic,
                topicMessage.ServiceName,
                EmptyServiceProvider.Instance,
                cancellationToken);
            await _dispatcher.DispatchAsync(endpoint, message, context, cancellationToken);
        }
    }
}

internal sealed class ZLinkFrameworkRuntimeState(
    IZLinkBackendContext context,
    ZLinkFrameworkRegistration registration) : IAsyncDisposable
{
    public IZLinkBackendContext Context { get; } = context;

    public ZLinkFrameworkRegistration Registration { get; } = registration;

    public object SyncRoot { get; } = new();

    public CancellationTokenSource StopTokenSource { get; } = new();

    public Dictionary<string, ZLinkChannelRuntimeBundle> ServerBundles { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkChannelRuntimeBundle> SubscriberBundles { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkChannelRuntimeBundle> PublisherBundles { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkChannelRuntimeBundle> ClientBundles { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkSpotNodeRuntime> SpotNodes { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, global::Zlink.Discovery> SpotDiscoveries { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkStreamNodeRuntime> StreamNodes { get; } = new(StringComparer.Ordinal);

    public List<Task> ListenerTasks { get; } = [];

    public async ValueTask DisposeAsync()
    {
        StopTokenSource.Cancel();

        foreach (var bundle in ClientBundles.Values)
        {
            await DisposeSafelyAsync(bundle);
        }

        foreach (var bundle in PublisherBundles.Values)
        {
            await DisposeSafelyAsync(bundle);
        }

        foreach (var bundle in SubscriberBundles.Values)
        {
            await DisposeSafelyAsync(bundle);
        }

        foreach (var bundle in ServerBundles.Values)
        {
            await DisposeSafelyAsync(bundle);
        }

        foreach (var node in SpotNodes.Values)
        {
            await DisposeSafelyAsync(node);
        }

        foreach (var stream in StreamNodes.Values)
        {
            await DisposeSafelyAsync(stream);
        }

        foreach (var discovery in SpotDiscoveries.Values)
        {
            await DisposeSafelyAsync(discovery);
        }

        StopTokenSource.Dispose();
        await DisposeSafelyAsync(Context);
    }

    private static async ValueTask DisposeSafelyAsync(IAsyncDisposable disposable)
    {
        try
        {
            await disposable.DisposeAsync();
        }
        catch (ObjectDisposedException)
        {
        }
        catch (global::Zlink.ZlinkCloseException)
        {
        }
    }
}

internal sealed class EmptyServiceProvider : IServiceProvider
{
    public static readonly EmptyServiceProvider Instance = new();

    public object? GetService(Type serviceType)
    {
        _ = serviceType;
        return null;
    }
}
