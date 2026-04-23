using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend;
using System.Reflection;
using System.Threading;

namespace Zlink.Framework;

internal static class ZLinkSpotAmbientContext
{
    private static readonly AsyncLocal<ZLinkSpotActivation?> Current = new();

    public static ZLinkSpotActivation RequireCurrent()
    {
        return Current.Value
            ?? throw new InvalidOperationException(
                "IZLinkSpotClient can only be used inside an active SPOT callback.");
    }

    public static IDisposable Push(ZLinkSpotActivation activation)
    {
        var previous = Current.Value;
        Current.Value = activation;
        return new Revert(previous);
    }

    private sealed class Revert(ZLinkSpotActivation? previous) : IDisposable
    {
        public void Dispose()
        {
            Current.Value = previous;
        }
    }
}

internal sealed class ZLinkSpotDescriptor
{
    public required Type HandlerType { get; init; }

    public required Type SpotType { get; init; }

    public required Type MessageType { get; init; }

    public required MethodInfo HandleMethod { get; init; }

    public required string PacketName { get; init; }

    public Type? ReplyType { get; init; }

    public bool IsRequest => ReplyType is not null;
}

internal sealed class ZLinkSpotSubscriptionDescriptor
{
    public required string Topic { get; init; }

    public required Type HandlerType { get; init; }

    public required Type SpotType { get; init; }

    public required Type MessageType { get; init; }

    public required MethodInfo HandleMethod { get; init; }

    public required string PacketName { get; init; }
}

internal sealed class ZLinkSpotTimerDescriptor
{
    public required string Name { get; init; }

    public required TimeSpan Period { get; init; }

    public required Type HandlerType { get; init; }

    public required Type SpotType { get; init; }

    public required MethodInfo HandleMethod { get; init; }
}

internal sealed class ZLinkSpotActorJoinDescriptor
{
    public required Type HandlerType { get; init; }

    public required Type SpotType { get; init; }

    public required Type RequestType { get; init; }

    public required Type ReplyType { get; init; }

    public required MethodInfo HandleMethod { get; init; }

    public required string PacketName { get; init; }
}

internal sealed class ZLinkSpotPublisherBundle : IAsyncDisposable
{
    public ZLinkSpotPublisherBundle(IZLinkBackendSpot spot)
    {
        Spot = spot;
    }

    public IZLinkBackendSpot Spot { get; }

    public async ValueTask DisposeAsync()
    {
        await Spot.DisposeAsync();
    }
}

internal sealed class ZLinkSpotAttachedChannelBundle : IAsyncDisposable
{
    private readonly HashSet<string> _manualConnections = new(StringComparer.Ordinal);

    public ZLinkSpotAttachedChannelBundle(IZLinkBackendDealerSocket socket)
    {
        Socket = socket;
    }

    public IZLinkBackendDealerSocket Socket { get; }

    public IZLinkBackendDiscovery? Discovery { get; set; }

    public bool TryAddManualConnection(string endpoint)
    {
        lock (_manualConnections)
        {
            return _manualConnections.Add(endpoint);
        }
    }

    public void RemoveManualConnection(string endpoint)
    {
        lock (_manualConnections)
        {
            _manualConnections.Remove(endpoint);
        }
    }

    public IReadOnlyList<string> ListManualConnections()
    {
        lock (_manualConnections)
        {
            return _manualConnections.OrderBy(static endpoint => endpoint, StringComparer.Ordinal).ToArray();
        }
    }

    public async ValueTask DisposeAsync()
    {
        if (Discovery is not null)
        {
            await Discovery.DisposeAsync();
        }

        await Socket.DisposeAsync();
    }
}

internal sealed class ZLinkSpotNodeRuntime : IAsyncDisposable
{
    private readonly IServiceProvider _services;
    private readonly ZLinkFrameworkRegistration _frameworkRegistration;
    private readonly ZLinkSpotNodeRegistration _registration;
    private readonly string _spotChannelName;
    private readonly IZLinkBackendContext _context;
    private readonly IZLinkChannelBackendAdapter _channelAdapter;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly object _connectionsGate = new();
    private readonly Dictionary<string, ZLinkSpotAttachedChannelBundle> _channelBundles = new(StringComparer.Ordinal);
    private readonly Dictionary<string, ZLinkSpotPublisherBundle> _publisherBundles = new(StringComparer.Ordinal);
    private readonly Dictionary<global::Zlink.RoutingId, ZLinkSpotActivation> _spots = [];
    private readonly HashSet<string> _routerManualConnections = new(StringComparer.Ordinal);
    private readonly HashSet<string> _pubSubManualConnections = new(StringComparer.Ordinal);

    public ZLinkSpotNodeRuntime(
        IServiceProvider services,
        ZLinkFrameworkRegistration frameworkRegistration,
        ZLinkSpotNodeRegistration registration,
        IZLinkBackendContext context,
        IZLinkChannelBackendAdapter channelAdapter,
        IZLinkBackendSpotNode node,
        string spotChannelName)
    {
        _services = services;
        _frameworkRegistration = frameworkRegistration;
        _registration = registration;
        _context = context;
        _channelAdapter = channelAdapter;
        Node = node;
        _spotChannelName = spotChannelName;
    }

    public string Name => _registration.SpotNodeName;

    public IReadOnlyDictionary<string, Type> SpotFactories => _registration.SpotFactories;

    public IZLinkBackendSpotNode Node { get; }

    public IReadOnlyDictionary<string, ZLinkSpotAttachedChannelBundle> AttachedChannelBundles => _channelBundles;

    public IReadOnlyDictionary<string, ZLinkSpotPublisherBundle> PublisherBundles => _publisherBundles;

    public IReadOnlyCollection<ZLinkSpotActivation> Spots => _spots.Values;

    public ZLinkSpotMonitoringSnapshot GetMonitoringSnapshot()
    {
        return new ZLinkSpotMonitoringSnapshot(
            Node.StatusSnapshot(),
            Node.PeersSnapshot()
                .OrderBy(static entry => entry.PeerEndpoint, StringComparer.Ordinal)
                .ThenBy(static entry => entry.ServiceName, StringComparer.Ordinal)
                .ToArray(),
            Node.SubjectsSnapshot()
                .OrderBy(static entry => entry.Subject, StringComparer.Ordinal)
                .ThenBy(static entry => entry.Role)
                .ToArray());
    }

    public void AddChannelBundle(string channelName, ZLinkSpotAttachedChannelBundle bundle)
    {
        _channelBundles.Add(channelName, bundle);
    }

    public void AddPublisherBundle(string channelName, ZLinkSpotPublisherBundle bundle)
    {
        _publisherBundles.Add(channelName, bundle);
    }

    public ZLinkSpotAttachedChannelBundle GetOrCreateAttachedChannelBundle(string channelName)
    {
        if (_channelBundles.TryGetValue(channelName, out var existing))
        {
            return existing;
        }

        if (!_registration.AttachedChannelClients.TryGetValue(channelName, out var attached))
        {
            throw new InvalidOperationException(
                $"SPOT node '{Name}' attached channel client '{channelName}' is not registered.");
        }

        var dealer = _channelAdapter.CreateDealerSocket(_context);
        dealer.SetChannelName(attached.ChannelName);
        var bundle = new ZLinkSpotAttachedChannelBundle(dealer);

        if (attached.ManualConnections.Count > 0)
        {
            foreach (var endpoint in attached.ManualConnections)
            {
                dealer.Connect(endpoint);
                _ = bundle.TryAddManualConnection(endpoint);
            }

            Node.AttachChannelDealerManual(attached.ChannelName, dealer);
        }
        else
        {
            var discovery = _channelAdapter.CreateDiscovery(
                _context,
                ZLinkBackendServiceType.Socket,
                attached.ChannelName);
            foreach (var endpoint in _frameworkRegistration.Discovery?.Endpoints ?? [])
            {
                discovery.ConnectRegistry(endpoint);
            }

            Node.AttachChannelDealer(discovery, dealer);
            bundle.Discovery = discovery;
        }

        _channelBundles.Add(channelName, bundle);
        return bundle;
    }

    public ZLinkSpotPublisherBundle GetOrCreatePublisherBundle(string channelName)
    {
        if (_publisherBundles.TryGetValue(channelName, out var existing))
        {
            return existing;
        }

        if (!_registration.AttachedSpotPublisherClients.TryGetValue(channelName, out var attached))
        {
            throw new InvalidOperationException(
                $"SPOT node '{Name}' publisher client '{channelName}' is not registered.");
        }

        var publisher = Node.CreateSpot();
        var bundle = new ZLinkSpotPublisherBundle(publisher);

        if (attached.ManualConnections.Count > 0)
        {
            foreach (var endpoint in attached.ManualConnections)
            {
                if (TryAddPubSubConnection(endpoint))
                {
                    Node.ConnectPeer(endpoint);
                }
            }
        }

        _publisherBundles.Add(channelName, bundle);
        return bundle;
    }

    public async ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        global::Zlink.RoutingId? requestedSpotRid,
        CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            if (!_registration.SpotFactories.TryGetValue(spotName, out var spotType))
            {
                throw new InvalidOperationException(
                    $"SPOT factory '{spotName}' is not registered on node '{Name}'.");
            }

            foreach (var channelName in _registration.AttachedChannelClients.Keys)
            {
                GetOrCreateAttachedChannelBundle(channelName);
            }

            if (requestedSpotRid is global::Zlink.RoutingId existingRid
                && _spots.TryGetValue(existingRid, out var existing))
            {
                if (!string.Equals(existing.SpotName, spotName, StringComparison.Ordinal))
                {
                    throw new InvalidOperationException(
                        $"SPOT routing id '{existingRid}' already belongs to '{existing.SpotName}'.");
                }

                return new ZLinkSpotCreateResult(existing.SpotRid, existing.SpotName, false);
            }

            var nativeSpot = Node.CreateSpot();
            var spotCreated = false;
            try
            {
                if (requestedSpotRid is global::Zlink.RoutingId providedRid)
                {
                    nativeSpot.SetRoutingId(providedRid);
                }

                var spotScope = _services.CreateAsyncScope();
                try
                {
                    var spot = (ZLinkSpot)ActivatorUtilities.CreateInstance(
                        spotScope.ServiceProvider,
                        spotType,
                        nativeSpot.RoutingId,
                        Node.RoutingId);

                    var activation = new ZLinkSpotActivation(
                        spotScope,
                        spot,
                        nativeSpot,
                        spotName,
                        _spotChannelName,
                        _frameworkRegistration.DefaultTimeout);

                    spot.AttachRuntime(activation);
                    activation.BindDescriptors();
                    await activation.InitializeAsync(cancellationToken);
                    _spots.Add(activation.SpotRid, activation);
                    spotCreated = true;

                    return new ZLinkSpotCreateResult(activation.SpotRid, spotName, true);
                }
                catch
                {
                    await spotScope.DisposeAsync();
                    throw;
                }
            }
            finally
            {
                if (!spotCreated)
                {
                    await nativeSpot.DisposeAsync();
                }
            }
        }
        finally
        {
            _gate.Release();
        }
    }

    public ValueTask<ZLinkSpotInfo?> GetAsync(
        global::Zlink.RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        if (_spots.TryGetValue(spotRid, out var activation))
        {
            return ValueTask.FromResult<ZLinkSpotInfo?>(
                new ZLinkSpotInfo(activation.SpotRid, activation.SpotName));
        }

        return ValueTask.FromResult<ZLinkSpotInfo?>(null);
    }

    public ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        IReadOnlyList<ZLinkSpotInfo> items = _spots.Values
            .Select(static activation => new ZLinkSpotInfo(activation.SpotRid, activation.SpotName))
            .OrderBy(static item => item.SpotName, StringComparer.Ordinal)
            .ToArray();
        return ValueTask.FromResult(items);
    }

    public async ValueTask<bool> RemoveAsync(
        global::Zlink.RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            if (!_spots.Remove(spotRid, out var activation))
            {
                return false;
            }

            await activation.DisposeAsync();
            return true;
        }
        finally
        {
            _gate.Release();
        }
    }

    public ValueTask<bool> ConnectRouterAsync(string endpoint, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!TryAddRouterConnection(endpoint))
        {
            return ValueTask.FromResult(false);
        }

        Node.ConnectPeer(endpoint);
        return ValueTask.FromResult(true);
    }

    public ValueTask<bool> ConnectPubSubAsync(string endpoint, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!TryAddPubSubConnection(endpoint))
        {
            return ValueTask.FromResult(false);
        }

        Node.ConnectPeer(endpoint);
        return ValueTask.FromResult(true);
    }

    public void DisconnectRouter(string endpoint)
    {
        Node.DisconnectPeer(endpoint);
        RemoveRouterConnection(endpoint);
    }

    public void DisconnectPubSub(string endpoint)
    {
        Node.DisconnectPeer(endpoint);
        RemovePubSubConnection(endpoint);
    }

    public IReadOnlyList<string> ListRouterConnections()
    {
        lock (_connectionsGate)
        {
            return _routerManualConnections.OrderBy(static endpoint => endpoint, StringComparer.Ordinal).ToArray();
        }
    }

    public IReadOnlyList<string> ListPubSubConnections()
    {
        lock (_connectionsGate)
        {
            return _pubSubManualConnections.OrderBy(static endpoint => endpoint, StringComparer.Ordinal).ToArray();
        }
    }

    public async ValueTask DisposeAsync()
    {
        foreach (var activation in _spots.Values.ToArray())
        {
            await activation.DisposeAsync();
        }

        foreach (var publisher in _publisherBundles.Values)
        {
            await publisher.DisposeAsync();
        }

        foreach (var channel in _channelBundles.Values)
        {
            await channel.DisposeAsync();
        }

        await Node.DisposeAsync();
        _gate.Dispose();
    }

    private bool TryAddRouterConnection(string endpoint)
    {
        lock (_connectionsGate)
        {
            return _routerManualConnections.Add(endpoint);
        }
    }

    private bool TryAddPubSubConnection(string endpoint)
    {
        lock (_connectionsGate)
        {
            return _pubSubManualConnections.Add(endpoint);
        }
    }

    private void RemoveRouterConnection(string endpoint)
    {
        lock (_connectionsGate)
        {
            _routerManualConnections.Remove(endpoint);
        }
    }

    private void RemovePubSubConnection(string endpoint)
    {
        lock (_connectionsGate)
        {
            _pubSubManualConnections.Remove(endpoint);
        }
    }
}

internal sealed record ZLinkSpotMonitoringSnapshot(
    ZLinkSpotNodeStatus Status,
    IReadOnlyList<ZLinkSpotNodePeerEntry> Peers,
    IReadOnlyList<ZLinkSpotNodeSubjectEntry> Subjects);

internal sealed class ZLinkSpotActivation : ZLinkSpotRuntimeContext, IAsyncDisposable
{
    private readonly AsyncServiceScope _scope;
    private readonly SemaphoreSlim _executionGate = new(1, 1);
    private readonly object _dispatchQueueGate = new();
    private readonly CancellationTokenSource _stopSource = new();
    private readonly Dictionary<string, List<ZLinkSpotSubscriptionDescriptor>> _subscriptionsByTopic = new(StringComparer.Ordinal);
    private readonly Dictionary<string, ZLinkSpotDescriptor> _packetsByName = new(StringComparer.Ordinal);
    private readonly Dictionary<Type, ZLinkSpotActorJoinDescriptor> _actorJoinsByRequestType = [];
    private readonly List<IZLinkTimer> _timers = [];
    private readonly TimeSpan _defaultTimeout;
    private static readonly TimeSpan SubscriptionPollInterval = TimeSpan.FromMilliseconds(20);
    private Task? _subscriptionPump;
    private int _disposed;
    private int _subscriptionMessageCount;
    private int _subscriptionDispatchCount;
    private int _subscriptionIgnoreCount;
    private Task _dispatchQueue = Task.CompletedTask;

    public ZLinkSpotActivation(
        AsyncServiceScope scope,
        ZLinkSpot spot,
        IZLinkBackendSpot nativeSpot,
        string spotName,
        string channelName,
        TimeSpan defaultTimeout)
    {
        _scope = scope;
        Spot = spot;
        NativeSpot = nativeSpot;
        SpotName = spotName;
        ChannelName = channelName;
        _defaultTimeout = defaultTimeout;
    }

    public ZLinkSpot Spot { get; }

    public IZLinkBackendSpot NativeSpot { get; }

    public string SpotName { get; }

    public string ChannelName { get; }

    public global::Zlink.RoutingId SpotRid => NativeSpot.RoutingId;

    public IServiceProvider Services => _scope.ServiceProvider;

    public int SubscriptionMessageCount => Volatile.Read(ref _subscriptionMessageCount);

    public int SubscriptionDispatchCount => Volatile.Read(ref _subscriptionDispatchCount);

    public int SubscriptionIgnoreCount => Volatile.Read(ref _subscriptionIgnoreCount);

    public string? LastSubscriptionTopic { get; private set; }

    public string? LastSubscriptionPacketName { get; private set; }

    public bool IsDisposed => Volatile.Read(ref _disposed) != 0;

    public void BindDescriptors()
    {
        foreach (var packet in Spot.Packets)
        {
            var descriptor = CreatePacketDescriptor(packet.HandlerType, Spot.GetType());
            _packetsByName.Add(descriptor.PacketName, descriptor);
        }

        foreach (var subscription in Spot.Subscriptions)
        {
            var descriptor = CreateSubscriptionDescriptor(
                subscription.Topic,
                subscription.HandlerType,
                Spot.GetType());

            if (!_subscriptionsByTopic.TryGetValue(subscription.Topic, out var handlers))
            {
                handlers = [];
                _subscriptionsByTopic.Add(subscription.Topic, handlers);
            }

            handlers.Add(descriptor);
            NativeSpot.SetSubscription(subscription.Topic);
        }

        foreach (var actorJoin in Spot.ActorJoins)
        {
            var descriptor = CreateActorJoinDescriptor(
                actorJoin.HandlerType,
                Spot.GetType(),
                actorJoin.RequestType,
                actorJoin.ReplyType);

            if (!_actorJoinsByRequestType.TryAdd(descriptor.RequestType, descriptor))
            {
                throw new InvalidOperationException(
                    $"SPOT actor join request '{descriptor.RequestType}' is already registered on '{Spot.GetType()}'.");
            }
        }
    }

    public async ValueTask InitializeAsync(CancellationToken cancellationToken)
    {
        if (_packetsByName.Count > 0)
        {
            RegisterWithoutSynchronizationContext(() =>
            {
                NativeSpot.OnDispatchEvent(info =>
                {
                    if (info.Event == ZLinkBackendSpotDispatchEvent.RoutedReadable)
                    {
                        QueueSerialized(
                            static (activation, ct) => activation.DispatchRoutedDrainAsync(ct));
                    }
                    else if (info.Event == ZLinkBackendSpotDispatchEvent.ChannelReplyReadable
                        && info.Subject is IntPtr dealerSubject
                        && dealerSubject != IntPtr.Zero)
                    {
                        NativeSpot.DrainChannelReplyFrom(dealerSubject);
                    }
                });

                return 0;
            });
        }

        if (_subscriptionsByTopic.Count > 0)
        {
            _subscriptionPump = Task.Run(
                () => RunSubscriptionLoopAsync(StopToken),
                CancellationToken.None);
        }

        await ExecuteSerializedAsync(
            static (activation, ct) => activation.Spot.OnInitializeAsync(ct),
            cancellationToken);
    }

    public IZLinkPublishCall Publish<TEvent>(string topic, TEvent message)
    {
        return new ZLinkCurrentSpotPublishCall<TEvent>(this, topic, message);
    }

    public async ValueTask<IZLinkTimer> AddTimerAsync<THandler>(
        string name,
        TimeSpan period,
        CancellationToken cancellationToken)
        where THandler : class
    {
        cancellationToken.ThrowIfCancellationRequested();

        if (string.IsNullOrWhiteSpace(name))
        {
            throw new ZLinkConfigurationException("SPOT timer name must not be empty.");
        }

        if (period <= TimeSpan.Zero)
        {
            throw new ZLinkConfigurationException("SPOT timer period must be greater than zero.");
        }

        var descriptor = CreateTimerDescriptor(name, typeof(THandler), Spot.GetType());
        var timer = new ZLinkTimer(
            period,
            StopToken,
            ct => ExecuteSerializedAsync(
                async static (activation, state, innerCt) =>
                {
                    await activation.InvokeTimerAsync(state, innerCt);
                },
                descriptor,
                ct));
        _timers.Add(timer);
        return timer;
    }

    public async ValueTask<IReadOnlyList<global::Zlink.Message>> RequestChannelAsync(
        string channelName,
        global::Zlink.Message message,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        return await NativeSpot.RequestChannelAsync(
            channelName,
            message,
            timeout ?? _defaultTimeout,
            cancellationToken);
    }

    public bool SendChannel(
        string channelName,
        global::Zlink.Message message,
        global::Zlink.SendFlags flags)
    {
        return NativeSpot.SendChannel(channelName, message, flags);
    }

    public bool PublishCurrent(
        string topic,
        global::Zlink.Message message,
        global::Zlink.SendFlags flags)
    {
        return NativeSpot.Publish(ChannelName, topic, message, flags);
    }

    public bool SendToSpot(
        global::Zlink.RoutingId targetRid,
        global::Zlink.RoutingId spotRid,
        global::Zlink.Message message,
        global::Zlink.SendFlags flags)
    {
        return NativeSpot.SendToSpot(targetRid, spotRid, message, flags);
    }

    public CancellationToken StopToken => _stopSource.Token;

    public async ValueTask<TReply> JoinActorAsync<TRequest, TReply>(
        IZLinkActor actor,
        TRequest request,
        CancellationToken cancellationToken)
        where TRequest : IZLinkRequest<TReply>
    {
        if (!_actorJoinsByRequestType.TryGetValue(typeof(TRequest), out var descriptor))
        {
            throw new InvalidOperationException(
                $"SPOT '{Spot.GetType()}' does not register an actor join handler for '{typeof(TRequest)}'.");
        }

        var state = new ActorJoinCallState(actor, request, descriptor);
        await ExecuteSerializedAsync(
            async static (activation, state, ct) =>
            {
                state.Reply = await activation.InvokeActorJoinAsync(
                    state.Descriptor,
                    state.Actor,
                    state.Request,
                    ct);
            },
            state,
            cancellationToken);

        return (TReply?)state.Reply
            ?? throw new InvalidOperationException(
                $"SPOT actor join reply for '{typeof(TRequest)}' was null.");
    }

    public async ValueTask SubmitActorAsync(
        IZLinkActor actor,
        global::Zlink.Message header,
        global::Zlink.Message body,
        CancellationToken cancellationToken)
    {
        var ownedHeader = header.Move();
        var ownedBody = body.Move();

        try
        {
            await ExecuteSerializedAsync(
                async static (activation, state, ct) =>
                {
                    using var currentHeader = state.Header;
                    using var currentBody = state.Body;
                    await state.Actor.OnDispatchAsync(
                        currentHeader.Move(),
                        currentBody.Move(),
                        ct);
                },
                new ActorDispatchState(actor, ownedHeader, ownedBody),
                cancellationToken);
        }
        catch
        {
            ownedHeader.Dispose();
            ownedBody.Dispose();
            throw;
        }
    }

    public ValueTask DisconnectActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        return ExecuteSerializedAsync(
            static (activation, state, ct) => state.OnDisconnectedAsync(ct),
            actor,
            cancellationToken);
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return;
        }

        _stopSource.Cancel();
        if (_subscriptionPump is not null)
        {
            try
            {
                await _subscriptionPump;
            }
            catch (OperationCanceledException)
            {
            }
            catch (ObjectDisposedException)
            {
            }
        }

        Task pendingDispatch;
        lock (_dispatchQueueGate)
        {
            pendingDispatch = _dispatchQueue;
        }

        try
        {
            await pendingDispatch;
        }
        catch (OperationCanceledException)
        {
        }
        catch (ObjectDisposedException)
        {
        }

        foreach (var timer in _timers)
        {
            await timer.DisposeAsync();
        }

        await NativeSpot.DisposeAsync();
        _stopSource.Dispose();
        _executionGate.Dispose();
        await _scope.DisposeAsync();
    }

    private async ValueTask ExecuteSerializedAsync(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        if (Volatile.Read(ref _disposed) != 0)
        {
            return;
        }

        await _executionGate.WaitAsync(cancellationToken);
        try
        {
            if (Volatile.Read(ref _disposed) != 0)
            {
                return;
            }

            using var _ = ZLinkSpotAmbientContext.Push(this);
            await operation(this, cancellationToken);
        }
        finally
        {
            _executionGate.Release();
        }
    }

    private async ValueTask ExecuteSerializedAsync<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        CancellationToken cancellationToken)
    {
        if (Volatile.Read(ref _disposed) != 0)
        {
            return;
        }

        await _executionGate.WaitAsync(cancellationToken);
        try
        {
            if (Volatile.Read(ref _disposed) != 0)
            {
                return;
            }

            using var _ = ZLinkSpotAmbientContext.Push(this);
            await operation(this, state, cancellationToken);
        }
        finally
        {
            _executionGate.Release();
        }
    }

    private void QueueSerialized(Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation)
    {
        lock (_dispatchQueueGate)
        {
            _dispatchQueue = _dispatchQueue.ContinueWith(
                static async (_, state) =>
                {
                    var activation = ((ZLinkSpotActivation Activation, Func<ZLinkSpotActivation, CancellationToken, ValueTask> Operation))state!;

                    try
                    {
                        await activation.Operation(
                            activation.Activation,
                            activation.Activation.StopToken).ConfigureAwait(false);
                    }
                    catch (OperationCanceledException) when (activation.Activation.StopToken.IsCancellationRequested)
                    {
                    }
                    catch (ObjectDisposedException)
                    {
                    }
                },
                (this, operation),
                CancellationToken.None,
                TaskContinuationOptions.None,
                TaskScheduler.Default).Unwrap();
        }
    }

    private async ValueTask DispatchRoutedAsync(global::Zlink.Received received, CancellationToken cancellationToken)
    {
        using (received)
        {
            if (received.Count == 0)
            {
                return;
            }

            var header = ZLinkEnvelopeCodec.DecodeHeader(received[0]);
            if (!_packetsByName.TryGetValue(header.PacketName, out var descriptor))
            {
                return;
            }

            var message = ZLinkEnvelopeCodec.DecodeBody(received[0], descriptor.MessageType);
            if (descriptor.IsRequest)
            {
                var reply = await InvokeRequestAsync(descriptor, message, cancellationToken);
                var replyHeader = new ZLinkEnvelopeHeader(
                    ZLinkMessageKind.Response,
                    ChannelName,
                    descriptor.PacketName,
                    ZLinkEnvelopeCodec.DefaultContentType,
                    header.CorrelationId,
                    null,
                    null,
                    null,
                    null);
                using var replyMessage = ZLinkEnvelopeCodec.Encode(replyHeader, reply, descriptor.ReplyType);
                received.Reply(replyMessage);
                return;
            }

            await InvokePacketAsync(descriptor, message, cancellationToken);
        }
    }

    private async ValueTask DispatchRoutedDrainAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            global::Zlink.Received received;
            try
            {
                received = NativeSpot.RecvRouted(global::Zlink.RecvFlags.DontWait);
            }
            catch (global::Zlink.ZlinkException ex)
                when (ex.InternalErrno == (int)global::Zlink.ErrorCode.EAgain)
            {
                return;
            }

            await DispatchRoutedAsync(received, cancellationToken);
        }
    }

    private async ValueTask DispatchSubscriptionsAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            using var message = NativeSpot.Subscribe(global::Zlink.RecvFlags.DontWait);
            if (message is null)
            {
                return;
            }

            await DispatchSubscriptionMessageAsync(message, cancellationToken);
        }
    }

    private async Task RunSubscriptionLoopAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                await ExecuteSerializedAsync(
                    static (activation, ct) => activation.DispatchSubscriptionsAsync(ct),
                    cancellationToken);
                await Task.Delay(SubscriptionPollInterval, cancellationToken);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (ObjectDisposedException)
            {
                return;
            }
            catch (global::Zlink.ZlinkException ex)
                when (cancellationToken.IsCancellationRequested
                      || ex.InternalErrno is (int)global::Zlink.ErrorCode.EFault
                      or (int)global::Zlink.ErrorCode.EBadf)
            {
                return;
            }
        }
    }

    private async ValueTask DispatchSubscriptionMessageAsync(
        global::Zlink.TopicMessage message,
        CancellationToken cancellationToken)
    {
        Interlocked.Increment(ref _subscriptionMessageCount);
        LastSubscriptionTopic = message.Topic;

        if (!_subscriptionsByTopic.TryGetValue(message.Topic, out var descriptors)
            || message.Parts.Count == 0)
        {
            Interlocked.Increment(ref _subscriptionIgnoreCount);
            return;
        }

        var header = ZLinkEnvelopeCodec.DecodeHeader(message.Parts[0]);
        LastSubscriptionPacketName = header.PacketName;
        var dispatched = false;
        foreach (var descriptor in descriptors)
        {
            if (!string.Equals(descriptor.PacketName, header.PacketName, StringComparison.Ordinal))
            {
                continue;
            }

            var body = ZLinkEnvelopeCodec.DecodeBody(message.Parts[0], descriptor.MessageType);
            await InvokeSubscriptionAsync(descriptor, body, cancellationToken);
            dispatched = true;
            Interlocked.Increment(ref _subscriptionDispatchCount);
        }

        if (!dispatched)
        {
            Interlocked.Increment(ref _subscriptionIgnoreCount);
        }
    }

    private async ValueTask InvokePacketAsync(
        ZLinkSpotDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        var handler = Services.GetRequiredService(descriptor.HandlerType);
        var result = descriptor.HandleMethod.Invoke(handler, [Spot, message, cancellationToken]);
        await AwaitResultAsync(result).ConfigureAwait(false);
    }

    private async ValueTask<object?> InvokeRequestAsync(
        ZLinkSpotDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        var handler = Services.GetRequiredService(descriptor.HandlerType);
        var result = descriptor.HandleMethod.Invoke(handler, [Spot, message, cancellationToken]);
        return await AwaitResultAsync(result).ConfigureAwait(false);
    }

    private async ValueTask InvokeSubscriptionAsync(
        ZLinkSpotSubscriptionDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        var handler = Services.GetRequiredService(descriptor.HandlerType);
        var result = descriptor.HandleMethod.Invoke(handler, [Spot, message, cancellationToken]);
        await AwaitResultAsync(result).ConfigureAwait(false);
    }

    private async ValueTask InvokeTimerAsync(
        ZLinkSpotTimerDescriptor descriptor,
        CancellationToken cancellationToken)
    {
        var handler = Services.GetRequiredService(descriptor.HandlerType);
        var result = descriptor.HandleMethod.Invoke(handler, [Spot, cancellationToken]);
        await AwaitResultAsync(result).ConfigureAwait(false);
    }

    private async ValueTask<object?> InvokeActorJoinAsync(
        ZLinkSpotActorJoinDescriptor descriptor,
        IZLinkActor actor,
        object request,
        CancellationToken cancellationToken)
    {
        var handler = Services.GetRequiredService(descriptor.HandlerType);
        var result = descriptor.HandleMethod.Invoke(handler, [Spot, actor, request, cancellationToken]);
        return await AwaitResultAsync(result).ConfigureAwait(false);
    }

    private static ZLinkSpotDescriptor CreatePacketDescriptor(Type handlerType, Type expectedSpotType)
    {
        foreach (var implemented in handlerType.GetInterfaces())
        {
            if (!implemented.IsGenericType)
            {
                continue;
            }

            var definition = implemented.GetGenericTypeDefinition();
            if (definition == typeof(IZLinkSpotPacketHandler<,>))
            {
                var arguments = implemented.GetGenericArguments();
                ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
                return new ZLinkSpotDescriptor
                {
                    HandlerType = handlerType,
                    SpotType = arguments[0],
                    MessageType = arguments[1],
                    HandleMethod = handlerType.GetMethod("HandleAsync")!,
                    PacketName = ZLinkPacketNameResolver.ResolveFromType(arguments[1]),
                };
            }

            if (definition == typeof(IZLinkSpotRequestHandler<,,>))
            {
                var arguments = implemented.GetGenericArguments();
                ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
                return new ZLinkSpotDescriptor
                {
                    HandlerType = handlerType,
                    SpotType = arguments[0],
                    MessageType = arguments[1],
                    ReplyType = arguments[2],
                    HandleMethod = handlerType.GetMethod("HandleAsync")!,
                    PacketName = ZLinkPacketNameResolver.ResolveFromType(arguments[1]),
                };
            }
        }

        throw new InvalidOperationException(
            $"SPOT packet handler '{handlerType}' must implement IZLinkSpotPacketHandler<,> or IZLinkSpotRequestHandler<,,>.");
    }

    private static ZLinkSpotSubscriptionDescriptor CreateSubscriptionDescriptor(
        string topic,
        Type handlerType,
        Type expectedSpotType)
    {
        foreach (var implemented in handlerType.GetInterfaces())
        {
            if (!implemented.IsGenericType
                || implemented.GetGenericTypeDefinition() != typeof(IZLinkSpotSubscriptionHandler<,>))
            {
                continue;
            }

            var arguments = implemented.GetGenericArguments();
            ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
            return new ZLinkSpotSubscriptionDescriptor
            {
                Topic = topic,
                HandlerType = handlerType,
                SpotType = arguments[0],
                MessageType = arguments[1],
                HandleMethod = handlerType.GetMethod("HandleAsync")!,
                PacketName = ZLinkPacketNameResolver.ResolveFromType(arguments[1]),
            };
        }

        throw new InvalidOperationException(
            $"SPOT subscription handler '{handlerType}' must implement IZLinkSpotSubscriptionHandler<,>.");
    }

    private static ZLinkSpotTimerDescriptor CreateTimerDescriptor(
        string name,
        Type handlerType,
        Type expectedSpotType)
    {
        foreach (var implemented in handlerType.GetInterfaces())
        {
            if (!implemented.IsGenericType
                || implemented.GetGenericTypeDefinition() != typeof(IZLinkSpotTimerHandler<>))
            {
                continue;
            }

            var arguments = implemented.GetGenericArguments();
            ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
            return new ZLinkSpotTimerDescriptor
            {
                Name = name,
                Period = TimeSpan.Zero,
                HandlerType = handlerType,
                SpotType = arguments[0],
                HandleMethod = handlerType.GetMethod("HandleAsync")!,
            };
        }

        throw new InvalidOperationException(
            $"SPOT timer handler '{handlerType}' must implement IZLinkSpotTimerHandler<>.");
    }

    private static ZLinkSpotActorJoinDescriptor CreateActorJoinDescriptor(
        Type handlerType,
        Type expectedSpotType,
        Type expectedRequestType,
        Type expectedReplyType)
    {
        foreach (var implemented in handlerType.GetInterfaces())
        {
            if (!implemented.IsGenericType
                || implemented.GetGenericTypeDefinition() != typeof(IZLinkSpotActorJoinHandler<,,>))
            {
                continue;
            }

            var arguments = implemented.GetGenericArguments();
            ValidateSpotType(handlerType, expectedSpotType, arguments[0]);

            if (arguments[1] != expectedRequestType || arguments[2] != expectedReplyType)
            {
                throw new InvalidOperationException(
                    $"SPOT actor join handler '{handlerType}' targets '{arguments[1]}/{arguments[2]}', but registration expects '{expectedRequestType}/{expectedReplyType}'.");
            }

            return new ZLinkSpotActorJoinDescriptor
            {
                HandlerType = handlerType,
                SpotType = arguments[0],
                RequestType = arguments[1],
                ReplyType = arguments[2],
                HandleMethod = handlerType.GetMethod("HandleAsync")!,
                PacketName = ZLinkPacketNameResolver.ResolveFromType(arguments[1]),
            };
        }

        throw new InvalidOperationException(
            $"SPOT actor join handler '{handlerType}' must implement IZLinkSpotActorJoinHandler<,,>.");
    }

    private static void ValidateSpotType(Type handlerType, Type expectedSpotType, Type actualSpotType)
    {
        if (actualSpotType != expectedSpotType)
        {
            throw new InvalidOperationException(
                $"SPOT handler '{handlerType}' targets '{actualSpotType}', but the runtime spot type is '{expectedSpotType}'.");
        }
    }

    private static async ValueTask<object?> AwaitResultAsync(object? result)
    {
        if (result is null)
        {
            return null;
        }

        switch (result)
        {
            case ValueTask valueTask:
                await valueTask.ConfigureAwait(false);
                return null;
            case Task task when result.GetType() == typeof(Task):
                await task.ConfigureAwait(false);
                return null;
        }

        var resultType = result.GetType();
        if (resultType.IsGenericType && resultType.GetGenericTypeDefinition() == typeof(Task<>))
        {
            var task = (Task)result;
            await task.ConfigureAwait(false);
            return resultType.GetProperty("Result")!.GetValue(result);
        }

        if (resultType.IsGenericType && resultType.GetGenericTypeDefinition() == typeof(ValueTask<>))
        {
            var asTask = (Task)resultType.GetMethod("AsTask")!.Invoke(result, null)!;
            await asTask.ConfigureAwait(false);
            return asTask.GetType().GetProperty("Result")!.GetValue(asTask);
        }

        return result;
    }

    private static T RegisterWithoutSynchronizationContext<T>(Func<T> action)
    {
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(null);
        try
        {
            return action();
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    private sealed class ActorJoinCallState(
        IZLinkActor actor,
        object request,
        ZLinkSpotActorJoinDescriptor descriptor)
    {
        public IZLinkActor Actor { get; } = actor;

        public object Request { get; } = request;

        public ZLinkSpotActorJoinDescriptor Descriptor { get; } = descriptor;

        public object? Reply { get; set; }
    }

    private sealed class ActorDispatchState(
        IZLinkActor actor,
        global::Zlink.Message header,
        global::Zlink.Message body)
    {
        public IZLinkActor Actor { get; } = actor;

        public global::Zlink.Message Header { get; } = header;

        public global::Zlink.Message Body { get; } = body;
    }
}

internal sealed class ZLinkCurrentSpotPublishCall<TEvent>(
    ZLinkSpotActivation activation,
    string topic,
    TEvent message) : IZLinkPublishCall
{
    private string? _packetName = ZLinkPacketNameResolver.ResolveFromMessage(message);
    private global::Zlink.SendFlags _flags;

    public IZLinkPublishCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkPublishCall WithDontWait()
    {
        _flags |= global::Zlink.SendFlags.DontWait;
        return this;
    }

    public bool Exec()
    {
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Event,
            activation.ChannelName,
            _packetName ?? throw new InvalidOperationException("Packet name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            null,
            null,
            topic,
            null,
            null);
        using var envelope = ZLinkEnvelopeCodec.Encode(header, message, message?.GetType() ?? typeof(TEvent));
        return activation.PublishCurrent(topic, envelope, _flags);
    }
}

internal sealed class ZLinkSpotPublisherClientService(ZLinkFrameworkRuntime runtime) : IZLinkSpotPublisherClient
{
    public IZLinkPublishCall Publish<TEvent>(string channelName, string topic, TEvent message)
    {
        return new ZLinkExternalSpotPublishCall<TEvent>(runtime, channelName, topic, message);
    }
}

internal sealed class ZLinkExternalSpotPublishCall<TEvent>(
    ZLinkFrameworkRuntime runtime,
    string channelName,
    string topic,
    TEvent message) : IZLinkPublishCall
{
    private string? _packetName = ZLinkPacketNameResolver.ResolveFromMessage(message);
    private global::Zlink.SendFlags _flags;

    public IZLinkPublishCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkPublishCall WithDontWait()
    {
        _flags |= global::Zlink.SendFlags.DontWait;
        return this;
    }

    public bool Exec()
    {
        var bundle = runtime.GetSpotPublisherBundle(channelName);
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Event,
            channelName,
            _packetName ?? throw new InvalidOperationException("Packet name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            null,
            null,
            topic,
            null,
            null);
        using var envelope = ZLinkEnvelopeCodec.Encode(header, message, message?.GetType() ?? typeof(TEvent));
        return bundle.Spot.Publish(channelName, topic, envelope, _flags);
    }
}

internal sealed class ZLinkSpotClientService(ZLinkFrameworkRuntime runtime) : IZLinkSpotClient
{
    public IZLinkSendCall SendChannel<TMessage>(string channelName, TMessage message)
    {
        return new ZLinkCurrentSpotSendCall<TMessage>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            channelName,
            message);
    }

    public IZLinkRequestCall<TReply> RequestChannel<TReply>(
        string channelName,
        IZLinkRequest<TReply> request)
    {
        return new ZLinkCurrentSpotRequestCall<TReply>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            channelName,
            request);
    }

    public IZLinkPublishCall Publish<TEvent>(string topic, TEvent message)
    {
        return ZLinkSpotAmbientContext.RequireCurrent().Publish(topic, message);
    }

    public ValueTask<TReply> JoinActorAsync<TRequest, TReply>(
        global::Zlink.RoutingId spotRid,
        IZLinkActor actor,
        TRequest request,
        CancellationToken cancellationToken = default)
        where TRequest : IZLinkRequest<TReply>
    {
        return runtime.JoinActorAsync<TRequest, TReply>(spotRid, actor, request, cancellationToken);
    }
}

internal sealed class ZLinkCurrentSpotSendCall<TMessage>(
    ZLinkSpotActivation activation,
    string channelName,
    TMessage message) : IZLinkSendCall
{
    private string? _packetName = ZLinkPacketNameResolver.ResolveFromMessage(message);
    private global::Zlink.SendFlags _flags;

    public IZLinkSendCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkSendCall WithDontWait()
    {
        _flags |= global::Zlink.SendFlags.DontWait;
        return this;
    }

    public bool Exec()
    {
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Command,
            channelName,
            _packetName ?? throw new InvalidOperationException("Packet name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            null,
            null,
            null,
            null);
        using var envelope = ZLinkEnvelopeCodec.Encode(header, message, message?.GetType() ?? typeof(TMessage));
        return activation.SendChannel(channelName, envelope, _flags);
    }
}

internal sealed class ZLinkCurrentSpotRequestCall<TReply>(
    ZLinkSpotActivation activation,
    string channelName,
    IZLinkRequest<TReply> request) : IZLinkRequestCall<TReply>
{
    private string? _packetName = ZLinkPacketNameResolver.ResolveFromMessage(request);
    private TimeSpan? _timeout;

    public IZLinkRequestCall<TReply> WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkRequestCall<TReply> WithTimeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public async ValueTask<TReply> ExecAsync(CancellationToken cancellationToken = default)
    {
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            channelName,
            _packetName ?? throw new InvalidOperationException("Packet name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            DateTimeOffset.UtcNow.Add(_timeout ?? TimeSpan.FromSeconds(30)),
            null,
            null,
            null);
        using var envelope = ZLinkEnvelopeCodec.Encode(header, request, request.GetType());
        var reply = await activation.RequestChannelAsync(channelName, envelope, _timeout, cancellationToken);
        try
        {
            if (reply.Count == 0)
            {
                throw new InvalidOperationException("SPOT channel request reply is empty.");
            }

            var replyHeader = ZLinkEnvelopeCodec.DecodeHeader(reply[0]);
            if (replyHeader.Kind == ZLinkMessageKind.Error)
            {
                throw new InvalidOperationException(replyHeader.ErrorMessage ?? "SPOT channel request failed.");
            }

            return (TReply?)ZLinkEnvelopeCodec.DecodeBody(reply[0], typeof(TReply))
                ?? throw new InvalidOperationException("SPOT channel reply body is null.");
        }
        finally
        {
            foreach (var item in reply)
            {
                item.Dispose();
            }
        }
    }
}

internal sealed class ZLinkSpotManagerService(ZLinkFrameworkRuntime runtime) : IZLinkSpotManager
{
    public ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        CancellationToken cancellationToken = default)
    {
        return runtime.CreateSpotAsync(spotName, null, cancellationToken);
    }

    public ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        global::Zlink.RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        return runtime.CreateSpotAsync(spotName, spotRid, cancellationToken);
    }

    public ValueTask<ZLinkSpotInfo?> GetAsync(
        global::Zlink.RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        return runtime.GetSpotAsync(spotRid, cancellationToken);
    }

    public ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(
        CancellationToken cancellationToken = default)
    {
        return runtime.ListSpotsAsync(cancellationToken);
    }

    public ValueTask<bool> RemoveAsync(
        global::Zlink.RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        return runtime.RemoveSpotAsync(spotRid, cancellationToken);
    }
}

internal sealed class ZLinkSpotConnectionManagerService(ZLinkFrameworkRuntime runtime)
    : IZLinkSpotConnectionManager
{
    public ValueTask<IZLinkEndpointConnections> GetRouterAsync(
        string spotNodeName,
        CancellationToken cancellationToken = default)
    {
        return runtime.GetSpotRouterConnectionsAsync(spotNodeName, cancellationToken);
    }

    public ValueTask<IZLinkEndpointConnections> GetPubSubAsync(
        string spotNodeName,
        CancellationToken cancellationToken = default)
    {
        return runtime.GetSpotPubSubConnectionsAsync(spotNodeName, cancellationToken);
    }

    public ValueTask<IZLinkEndpointConnections> GetChannelClientAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken = default)
    {
        return runtime.GetSpotChannelClientConnectionsAsync(
            spotNodeName,
            channelName,
            cancellationToken);
    }

    public ValueTask<IZLinkEndpointConnections> GetSpotPublisherClientAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken = default)
    {
        return runtime.GetSpotPublisherConnectionsAsync(
            spotNodeName,
            channelName,
            cancellationToken);
    }
}
