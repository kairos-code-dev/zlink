using Zlink.Framework.Backend;

namespace Zlink.Framework.Runtime;

internal sealed class ZLinkFrameworkRuntime
{
    private readonly IServiceProvider _services;
    private readonly IZLinkBackendAdapterFactory _backendAdapterFactory;
    private readonly ZLinkFrameworkRegistration _registration;
    private readonly ZLinkRegistryRuntime? _registryRuntime;
    private readonly ZLinkHandlerRegistry _handlerRegistry;
    private readonly ZLinkHandlerDispatcher _dispatcher;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly ZLinkActorSessionRegistry _actorSessions = new();
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
        return GetOrCreateClientBundle(state, channelName);
    }

    private ZLinkChannelRuntimeBundle GetOrCreateClientBundle(
        ZLinkFrameworkRuntimeState state,
        string channelName)
    {
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
                        _ = bundle.TryAddManualConnection(endpoint);
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
        return GetOrCreatePublisherBundle(state, channelName);
    }

    private ZLinkChannelRuntimeBundle GetOrCreatePublisherBundle(
        ZLinkFrameworkRuntimeState state,
        string channelName)
    {
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
            if (node.PublisherBundles.TryGetValue(channelName, out var bundle))
            {
                return bundle;
            }

            if (node.HasPublisherClient(channelName))
            {
                return node.GetOrCreatePublisherBundle(channelName);
            }
        }

        throw new InvalidOperationException(
            $"SPOT publisher client '{channelName}' is not registered.");
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

    internal async ValueTask<TReply> JoinActorAsync<TRequest, TReply>(
        global::Zlink.RoutingId spotRid,
        IZLinkActor actor,
        TRequest request,
        CancellationToken cancellationToken)
        where TRequest : IZLinkRequest<TReply>
    {
        var activation = GetSpotActivation(spotRid)
            ?? throw new InvalidOperationException($"SPOT '{spotRid}' is not active.");

        var reply = await activation.JoinActorAsync<TRequest, TReply>(actor, request, cancellationToken);

        var state = _actorSessions.GetOrCreate(actor.ActorKey);
        await state.Gate.WaitAsync(cancellationToken);
        try
        {
            state.Activation = activation;
        }
        finally
        {
            state.Gate.Release();
        }

        return reply;
    }

    internal async ValueTask AttachActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken)
    {
        var state = _actorSessions.GetOrCreate(actor.ActorKey);
        await state.Gate.WaitAsync(cancellationToken);
        try
        {
            state.SessionId = stream.SessionId;
            state.Stream = stream;
        }
        finally
        {
            state.Gate.Release();
        }
    }

    internal async ValueTask DisconnectActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken)
    {
        var actorKey = actor.ActorKey;
        var state = _actorSessions.GetOrCreate(actor.ActorKey);
        ZLinkSpotActivation? activation = null;

        await state.Gate.WaitAsync(cancellationToken);
        try
        {
            if (!string.Equals(state.SessionId, stream.SessionId, StringComparison.Ordinal))
            {
                return;
            }

            state.SessionId = null;
            state.Stream = null;
            activation = state.Activation;
            state.Activation = null;

            if (activation is not null && activation.IsDisposed)
            {
                activation = null;
            }
        }
        finally
        {
            state.Gate.Release();
        }

        if (activation is not null)
        {
            await activation.DisconnectActorAsync(actor, cancellationToken);
        }
        else
        {
            await actor.OnDisconnectedAsync(cancellationToken);
        }

        _actorSessions.TryRemove(actorKey, state);
    }

    internal async ValueTask SubmitActorAsync(
        IZLinkActor actor,
        ZlinkStreamHeader header,
        global::Zlink.Message body,
        CancellationToken cancellationToken)
    {
        var actorKey = actor.ActorKey;
        var state = _actorSessions.GetOrCreate(actor.ActorKey);
        ZLinkSpotActivation? activation;
        var shouldPrune = false;
        var context = new ZLinkActorContext(this, actor, state);

        await state.Gate.WaitAsync(cancellationToken);
        try
        {
            activation = state.Activation;

            if (activation is not null && activation.IsDisposed)
            {
                state.Activation = null;
                activation = null;
                shouldPrune = state.SessionId is null;
            }
        }
        finally
        {
            state.Gate.Release();
        }

        try
        {
            if (activation is null)
            {
                var previousDispatch = state.CurrentDispatch;
                state.CurrentDispatch = new ZLinkActorDispatchState(header);
                try
                {
                    await actor.OnDispatchAsync(context, header, body, cancellationToken);
                }
                finally
                {
                    state.CurrentDispatch = previousDispatch;
                }

                return;
            }

            await activation.SubmitActorAsync(actor, context, state, header, body, cancellationToken);
        }
        finally
        {
            if (shouldPrune)
            {
                _actorSessions.TryRemove(actorKey, state);
            }
        }
    }

    internal async ValueTask<IZLinkEndpointConnections> GetSpotRouterConnectionsAsync(
        string spotNodeName,
        CancellationToken cancellationToken)
    {
        var node = await GetSpotNodeAsync(spotNodeName, cancellationToken);
        return new ZLinkRuntimeConnections(
            (endpoint, token) => node.ConnectRouterAsync(endpoint, token),
            (endpoint, _) =>
            {
                node.DisconnectRouter(endpoint);
                return ValueTask.CompletedTask;
            },
            _ => ValueTask.FromResult<IReadOnlyList<string>>(node.ListRouterConnections()));
    }

    internal async ValueTask<IZLinkEndpointConnections> GetSpotPubSubConnectionsAsync(
        string spotNodeName,
        CancellationToken cancellationToken)
    {
        var node = await GetSpotNodeAsync(spotNodeName, cancellationToken);
        return new ZLinkRuntimeConnections(
            (endpoint, token) => node.ConnectPubSubAsync(endpoint, token),
            (endpoint, _) =>
            {
                node.DisconnectPubSub(endpoint);
                return ValueTask.CompletedTask;
            },
            _ => ValueTask.FromResult<IReadOnlyList<string>>(node.ListPubSubConnections()));
    }

    internal async ValueTask<IZLinkEndpointConnections> GetSpotChannelClientConnectionsAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken)
    {
        var node = await GetSpotNodeAsync(spotNodeName, cancellationToken);
        if (!node.AttachedChannelBundles.TryGetValue(channelName, out var bundle))
        {
            bundle = node.GetOrCreateAttachedChannelBundle(channelName);
        }

        return new ZLinkRuntimeConnections(
            (endpoint, _) =>
            {
                if (!bundle.TryAddManualConnection(endpoint))
                {
                    return ValueTask.FromResult(false);
                }

                bundle.Socket.Connect(endpoint);
                return ValueTask.FromResult(true);
            },
            (endpoint, _) =>
            {
                bundle.Socket.Disconnect(endpoint);
                bundle.RemoveManualConnection(endpoint);
                return ValueTask.CompletedTask;
            },
            _ => ValueTask.FromResult<IReadOnlyList<string>>(bundle.ListManualConnections()));
    }

    internal async ValueTask<IZLinkEndpointConnections> GetSpotPublisherConnectionsAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken)
    {
        var node = await GetSpotNodeAsync(spotNodeName, cancellationToken);
        if (!node.PublisherBundles.TryGetValue(channelName, out var bundle))
        {
            bundle = node.GetOrCreatePublisherBundle(channelName);
        }

        _ = bundle;

        return new ZLinkRuntimeConnections(
            (endpoint, token) => node.ConnectPubSubAsync(endpoint, token),
            (endpoint, _) =>
            {
                node.DisconnectPubSub(endpoint);
                return ValueTask.CompletedTask;
            },
            _ => ValueTask.FromResult<IReadOnlyList<string>>(node.ListPubSubConnections()));
    }

    internal async ValueTask<IZLinkEndpointConnections> GetClientConnectionsAsync(
        string channelName,
        CancellationToken cancellationToken)
    {
        var state = await GetStartedStateAsync(cancellationToken);

        if (!_registration.Channels.TryGetValue(channelName, out var channel)
            || channel.Client is null)
        {
            throw new InvalidOperationException($"Channel client '{channelName}' is not registered.");
        }

        return new ZLinkRuntimeConnections(
            (endpoint, token) =>
            {
                var bundle = GetOrCreateClientBundle(state, channelName);
                if (!bundle.TryAddManualConnection(endpoint))
                {
                    return ValueTask.FromResult(false);
                }

                ((IZLinkBackendDealerSocket)bundle.Socket).Connect(endpoint);
                return ValueTask.FromResult(true);
            },
            (endpoint, token) =>
            {
                var bundle = GetOrCreateClientBundle(state, channelName);
                ((IZLinkBackendDealerSocket)bundle.Socket).Disconnect(endpoint);
                bundle.RemoveManualConnection(endpoint);
                return ValueTask.CompletedTask;
            },
            token => ValueTask.FromResult<IReadOnlyList<string>>(
                GetOrCreateClientBundle(state, channelName).ListManualConnections()));
    }

    internal async ValueTask<IZLinkEndpointConnections> GetSubscriberConnectionsAsync(
        string channelName,
        CancellationToken cancellationToken)
    {
        var state = await GetStartedStateAsync(cancellationToken);
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

    internal ZLinkSpotMonitoringSnapshot GetSpotMonitoringSnapshot(string spotNodeName)
    {
        return GetSpotNode(spotNodeName).GetMonitoringSnapshot();
    }

    private async ValueTask<ZLinkFrameworkRuntimeState> GetStartedStateAsync(
        CancellationToken cancellationToken)
    {
        if (_state is null)
        {
            await StartAsync(cancellationToken);
        }

        return _state ?? throw new InvalidOperationException("ZLink framework runtime is not started.");
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
                        (IZLinkBackendRouterSocket)bundle.Socket,
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
                        (IZLinkBackendSubscriberSocket)bundle.Socket,
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

        var channelAdapter = _backendAdapterFactory.CreateChannelAdapter();
        var spotAdapter = _backendAdapterFactory.CreateSpotAdapter();

        foreach (var spotNodeRegistration in _registration.SpotNodes.Values)
        {
            var node = spotAdapter.CreateSpotNode(state.Context);
            node.Bind(spotNodeRegistration.BindEndpoint!);

            var runtime = new ZLinkSpotNodeRuntime(
                _services,
                _registration,
                spotNodeRegistration,
                state.Context,
                channelAdapter,
                node,
                _registration.SpotDiscovery?.ChannelName
                    ?? throw new InvalidOperationException("SPOT discovery is not configured."));

            if (_registration.SpotDiscovery is not null
                && _registration.SpotDiscovery.Endpoints.Count > 0)
            {
                var discovery = CreateDiscovery(
                    channelAdapter,
                    state,
                    _registration.SpotDiscovery.ChannelName,
                    ZLinkBackendServiceType.Spot,
                    _registration.SpotDiscovery.Endpoints);
                node.AttachDiscovery(discovery);
                state.SpotDiscoveries.Add($"{spotNodeRegistration.SpotNodeName}.discovery", discovery);
            }

            foreach (var endpoint in spotNodeRegistration.Router?.ManualConnections ?? [])
            {
                _ = runtime.ConnectRouterAsync(endpoint, CancellationToken.None);
            }

            foreach (var endpoint in spotNodeRegistration.PubSub?.ManualConnections ?? [])
            {
                _ = runtime.ConnectPubSubAsync(endpoint, CancellationToken.None);
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

        var streamAdapter = _backendAdapterFactory.CreateStreamAdapter();
        var monitoringAdapter = _backendAdapterFactory.CreateMonitoringAdapter();
        foreach (var streamNodeRegistration in _registration.StreamNodes.Values)
        {
            var socket = streamAdapter.CreateStreamSocket(state.Context);
            socket.Bind(streamNodeRegistration.BindEndpoint!);
            var monitor = monitoringAdapter.OpenSocketMonitor(socket);

            var runtime = new ZLinkStreamNodeRuntime(
                streamNodeRegistration.StreamNodeName,
                _services,
                socket,
                monitor,
                streamNodeRegistration.HeaderSessionType);
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
                _ = bundle.TryAddManualConnection(endpoint);
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

    private async ValueTask<ZLinkSpotNodeRuntime> GetSpotNodeAsync(
        string spotNodeName,
        CancellationToken cancellationToken)
    {
        var state = await GetStartedStateAsync(cancellationToken);
        if (state.SpotNodes.TryGetValue(spotNodeName, out var node))
        {
            return node;
        }

        throw new InvalidOperationException($"SPOT node '{spotNodeName}' is not registered.");
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

    private ZLinkSpotActivation? GetSpotActivation(global::Zlink.RoutingId spotRid)
    {
        foreach (var node in GetOrStartState().SpotNodes.Values)
        {
            var activation = node.Spots.FirstOrDefault(current => current.SpotRid == spotRid);
            if (activation is not null)
            {
                return activation;
            }
        }

        return null;
    }

    private async Task RunServerLoopAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
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
        IZLinkBackendSubscriberSocket subscriber,
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
        IZLinkBackendRouterSocket router,
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
        IZLinkBackendRouterSocket router,
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
