using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Core;

internal sealed class ZLinkFrameworkRuntime
{
    private readonly IServiceProvider _services;
    private readonly IZLinkBackendAdapterFactory _backendAdapterFactory;
    private readonly ZLinkFrameworkRegistration _registration;
    private readonly ZLinkRegistryRuntime? _registryRuntime;
    private readonly ZLinkChannelMessagePump _channelMessagePump;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly ZLinkActorSessionManager _actorSessionManager;
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
        _registryRuntime = registryRuntime;
        _channelMessagePump = new ZLinkChannelMessagePump(handlerRegistry, dispatcher);
        _actorSessionManager = new ZLinkActorSessionManager(this, services);
    }

    public IZLinkBackendContext? Context => _state?.Context;

    public ZLinkFrameworkRegistration Registration => _registration;

    internal IServiceProvider Services => _services;

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
                _ = Task.Run(async () => await bundle.DisposeAsync().ConfigureAwait(false));
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
        RoutingId? spotRid,
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
        RoutingId spotRid,
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
        RoutingId spotRid,
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
        RoutingId spotRid,
        IZLinkActor actor,
        TRequest request,
        CancellationToken cancellationToken = default)
    {
        var activation = GetSpotActivation(spotRid)
            ?? throw new InvalidOperationException($"SPOT '{spotRid}' is not active.");

        return await activation.JoinActorAsync<TRequest, TReply>(actor, request, cancellationToken);
    }

    internal async ValueTask JoinActorToSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
        => await _actorSessionManager.JoinActorToSpotAsync(activation, actor, cancellationToken);

    internal async ValueTask LeaveActorFromSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
        => await _actorSessionManager.LeaveActorFromSpotAsync(activation, actor, cancellationToken);

    internal async ValueTask AttachActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
        => await _actorSessionManager.AttachActorAsync(actor, stream, cancellationToken);

    internal async ValueTask DisconnectActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
        => await _actorSessionManager.DisconnectActorAsync(actor, stream, cancellationToken);

    internal async ValueTask SubmitActorAsync(
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
        => await _actorSessionManager.SubmitActorAsync(actor, header, body, cancellationToken);

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
            throw new InvalidOperationException(
                "ZLink framework runtime is not started. Call StartAsync before using synchronous runtime APIs.");
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
                    () => _channelMessagePump.RunServerLoopAsync(
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
                    () => _channelMessagePump.RunSubscriberLoopAsync(
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
                this,
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

    private ZLinkSpotActivation? GetSpotActivation(RoutingId spotRid)
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

}
