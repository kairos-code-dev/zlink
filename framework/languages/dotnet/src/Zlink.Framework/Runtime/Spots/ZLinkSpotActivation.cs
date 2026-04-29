using Microsoft.Extensions.DependencyInjection;
using System.Threading.Channels;
using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActivation : IZLinkSpotContext, IAsyncDisposable
{
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly AsyncServiceScope _scope;
    private readonly SemaphoreSlim _executionGate = new(1, 1);
    private readonly Channel<Func<ValueTask>> _dispatchQueue =
        Channel.CreateUnbounded<Func<ValueTask>>(new UnboundedChannelOptions
        {
            SingleReader = true,
            SingleWriter = false
        });
    private readonly CancellationTokenSource _stopSource = new();
    private readonly List<ZLinkSpotPacketRegistration> _packets = [];
    private readonly List<ZLinkSpotSubscriptionRegistration> _subscriptions = [];
    private readonly List<ZLinkSpotActorJoinRegistration> _actorJoins = [];
    private readonly Dictionary<string, IZLinkActor> _actorsById = new(StringComparer.Ordinal);
    private readonly Dictionary<string, List<ZLinkSpotSubscriptionDescriptor>> _subscriptionsByTopic = new(StringComparer.Ordinal);
    private readonly Dictionary<string, ZLinkSpotDescriptor> _packetsByName = new(StringComparer.Ordinal);
    private readonly Dictionary<Type, ZLinkSpotActorJoinDescriptor> _actorJoinsByRequestType = [];
    private ZLinkSpotHandlerInvoker? _handlerInvoker;
    private IZLinkSpot? _spot;
    private readonly List<IZLinkTimer> _timers = [];
    private readonly TimeSpan _defaultTimeout;
    private static readonly TimeSpan SubscriptionPollInterval = TimeSpan.FromMilliseconds(20);
    private Task? _subscriptionPump;
    private readonly Task _dispatchPump;
    private int _disposed;
    private int _subscriptionMessageCount;
    private int _subscriptionDispatchCount;
    private int _subscriptionIgnoreCount;
    private bool _configurationOpen = true;

    public ZLinkSpotActivation(
        ZLinkFrameworkRuntime runtime,
        AsyncServiceScope scope,
        IZLinkBackendSpot nativeSpot,
        RoutingId nodeRid,
        string spotName,
        string channelName,
        TimeSpan defaultTimeout)
    {
        _runtime = runtime;
        _scope = scope;
        NativeSpot = nativeSpot;
        NodeRid = nodeRid;
        SpotName = spotName;
        ChannelName = channelName;
        _defaultTimeout = defaultTimeout;
        _dispatchPump = Task.Run(RunQueuedDispatchAsync);
    }

    public IZLinkSpot Spot => _spot
        ?? throw new InvalidOperationException("SPOT has not been attached to this context.");

    private ZLinkSpotHandlerInvoker HandlerInvoker => _handlerInvoker
        ?? throw new InvalidOperationException("SPOT has not been attached to this context.");

    public IZLinkBackendSpot NativeSpot { get; }

    public string SpotName { get; }

    public string ChannelName { get; }

    public RoutingId SpotRid => NativeSpot.RoutingId;

    public RoutingId NodeRid { get; }

    public IServiceProvider Services => _scope.ServiceProvider;

    public int SubscriptionMessageCount => Volatile.Read(ref _subscriptionMessageCount);

    public int SubscriptionDispatchCount => Volatile.Read(ref _subscriptionDispatchCount);

    public int SubscriptionIgnoreCount => Volatile.Read(ref _subscriptionIgnoreCount);

    public string? LastSubscriptionTopic { get; private set; }

    public string? LastSubscriptionMessageName { get; private set; }

    public bool IsDisposed => Volatile.Read(ref _disposed) != 0;

    public void AttachSpot(IZLinkSpot spot)
    {
        ArgumentNullException.ThrowIfNull(spot);
        if (_spot is not null)
        {
            throw new InvalidOperationException("SPOT has already been attached to this context.");
        }

        _spot = spot;
        _handlerInvoker = new ZLinkSpotHandlerInvoker(_scope.ServiceProvider, spot);
    }

    public void BindDescriptors()
    {
        _configurationOpen = false;

        foreach (var packet in _packets)
        {
            var descriptor = ZLinkSpotDescriptorFactory.CreatePacketDescriptor(packet.HandlerType, Spot.GetType());
            _packetsByName.Add(descriptor.MessageName, descriptor);
        }

        foreach (var subscription in _subscriptions)
        {
            var descriptor = ZLinkSpotDescriptorFactory.CreateSubscriptionDescriptor(
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

        foreach (var actorJoin in _actorJoins)
        {
            var descriptor = ZLinkSpotDescriptorFactory.CreateActorJoinDescriptor(
                actorJoin.HandlerType,
                Spot.GetType(),
                actorJoin.ActorType,
                actorJoin.RequestType,
                actorJoin.ReplyType);

            if (!_actorJoinsByRequestType.TryAdd(descriptor.RequestType, descriptor))
            {
                throw new InvalidOperationException(
                    $"SPOT actor join request '{descriptor.RequestType}' is already registered on '{Spot.GetType()}'.");
            }
        }
    }

    public void AddPacket<THandler>()
        where THandler : class
    {
        EnsureConfigurationOpen();
        _packets.Add(new ZLinkSpotPacketRegistration(typeof(THandler)));
    }

    public void AddSubscribe<THandler>(string topic)
        where THandler : class
    {
        EnsureConfigurationOpen();
        if (string.IsNullOrWhiteSpace(topic))
        {
            throw new ZLinkConfigurationException("SPOT subscription topic must not be empty.");
        }

        _subscriptions.Add(new ZLinkSpotSubscriptionRegistration(topic, typeof(THandler)));
    }

    public void AddActorJoin<THandler, TActor, TRequest, TReply>()
        where THandler : class
        where TActor : IZLinkActor
    {
        EnsureConfigurationOpen();
        _actorJoins.Add(new ZLinkSpotActorJoinRegistration(
            typeof(THandler),
            typeof(TActor),
            typeof(TRequest),
            typeof(TReply)));
    }

    public ValueTask JoinActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(actor);
        return ReferenceEquals(ZLinkSpotAmbientContext.CurrentOrDefault, this)
            ? JoinActorCoreAsync(actor, cancellationToken)
            : ExecuteSerializedAsync(
                static (activation, state, ct) => activation.JoinActorCoreAsync(state, ct),
                actor,
                cancellationToken);
    }

    public ValueTask LeaveActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(actor);
        return ReferenceEquals(ZLinkSpotAmbientContext.CurrentOrDefault, this)
            ? LeaveActorCoreAsync(actor, cancellationToken)
            : ExecuteSerializedAsync(
                static (activation, state, ct) => activation.LeaveActorCoreAsync(state, ct),
                actor,
                cancellationToken);
    }

    public async ValueTask InitializeAsync(CancellationToken cancellationToken)
    {
        if (_packetsByName.Count > 0 || _actorJoinsByRequestType.Count > 0)
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

    public IZLinkSendCall SendChannel<TMessage>(string channelName, TMessage message)
    {
        return new ZLinkCurrentSpotSendCall<TMessage>(this, channelName, message);
    }

    public IZLinkRequestCall RequestChannel<TRequest>(string channelName, TRequest request)
    {
        return new ZLinkCurrentSpotRequestCall<TRequest>(this, channelName, request);
    }

    public ValueTask<IZLinkTimer> AddTimer<THandler>(
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

        var descriptor = ZLinkSpotDescriptorFactory.CreateTimerDescriptor(name, typeof(THandler), Spot.GetType());
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
        return ValueTask.FromResult<IZLinkTimer>(timer);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestChannelAsync(
        string channelName,
        Message message,
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
        Message message,
        SendFlags flags)
    {
        return NativeSpot.SendChannel(channelName, message, flags);
    }

    public bool PublishCurrent(
        string topic,
        Message message,
        SendFlags flags)
    {
        return NativeSpot.Publish(ChannelName, topic, message, flags);
    }

    public bool SendToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        Message message,
        SendFlags flags)
    {
        return NativeSpot.SendToSpot(targetRid, spotRid, message, flags);
    }

    public CancellationToken StopToken => _stopSource.Token;

    public async ValueTask<TReply> JoinActorAsync<TRequest, TReply>(
        IZLinkActor actor,
        TRequest request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);

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
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        var ownedBody = body.Move();

        try
        {
            await ExecuteSerializedAsync(
                async static (activation, state, ct) =>
                {
                    using var currentBody = state.Body;
                    var previousDispatch = state.RuntimeState.CurrentDispatch;
                    state.RuntimeState.CurrentDispatch = new ZLinkActorDispatchState(state.Header);
                    try
                    {
                        await state.RuntimeState.DispatchAsync(
                            activation._runtime.Services,
                            state.Actor,
                            state.Header,
                            currentBody.Move(),
                            ct);
                    }
                    finally
                    {
                        state.RuntimeState.CurrentDispatch = previousDispatch;
                    }
                },
                new ActorDispatchState(actor, runtimeState, header, ownedBody),
                cancellationToken);
        }
        catch
        {
            ownedBody.Dispose();
            throw;
        }
    }

    public ValueTask CloseAsync(CancellationToken cancellationToken)
    {
        return ExecuteSerializedAsync(
            static (activation, ct) => activation.Spot.OnClosingAsync(ct),
            cancellationToken);
    }

    public ValueTask DisconnectActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        return ExecuteSerializedAsync(
            async static (activation, state, ct) =>
            {
                await activation.LeaveActorCoreAsync(state, ct);
                await state.OnDisconnectedAsync(ct);
            },
            actor,
            cancellationToken);
    }

    private async ValueTask JoinActorCoreAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        if (_actorsById.TryGetValue(actor.ActorId, out var existing)
            && !ReferenceEquals(existing, actor))
        {
            throw new InvalidOperationException(
                $"SPOT '{SpotName}' already has an actor with id '{actor.ActorId}'.");
        }

        _actorsById[actor.ActorId] = actor;
        await _runtime.JoinActorToSpotAsync(this, actor, cancellationToken);
    }

    private async ValueTask LeaveActorCoreAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        if (_actorsById.TryGetValue(actor.ActorId, out var existing)
            && ReferenceEquals(existing, actor))
        {
            _actorsById.Remove(actor.ActorId);
        }

        await _runtime.LeaveActorFromSpotAsync(this, actor, cancellationToken);
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

        _dispatchQueue.Writer.TryComplete();

        try
        {
            await _dispatchPump;
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

    private void EnsureConfigurationOpen()
    {
        if (!_configurationOpen)
        {
            throw new InvalidOperationException(
                "SPOT handler registration is only allowed while IZLinkSpot.Configure is running.");
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
        _dispatchQueue.Writer.TryWrite(() => ExecuteSerializedAsync(operation, StopToken));
    }

    private async Task RunQueuedDispatchAsync()
    {
        await foreach (var operation in _dispatchQueue.Reader.ReadAllAsync().ConfigureAwait(false))
        {
            try
            {
                await operation().ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (StopToken.IsCancellationRequested)
            {
            }
            catch (ObjectDisposedException)
            {
            }
        }
    }

    private async ValueTask DispatchRoutedAsync(Received received, CancellationToken cancellationToken)
    {
        using (received)
        {
            if (received.Count == 0)
            {
                return;
            }

            var header = ZLinkEnvelopeCodec.DecodeHeader(received[0]);
            if (!_packetsByName.TryGetValue(header.MessageName, out var descriptor))
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
                    descriptor.MessageName,
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
            Received received;
            try
            {
                received = NativeSpot.RecvRouted(RecvFlags.DontWait);
            }
            catch (ZlinkException ex)
                when (ex.InternalErrno == (int)ErrorCode.EAgain)
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
            using var message = NativeSpot.Subscribe(RecvFlags.DontWait);
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
            catch (ZlinkException ex)
                when (cancellationToken.IsCancellationRequested
                      || ex.InternalErrno is (int)ErrorCode.EFault
                      or (int)ErrorCode.EBadf)
            {
                return;
            }
        }
    }

    private async ValueTask DispatchSubscriptionMessageAsync(
        TopicMessage message,
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
        LastSubscriptionMessageName = header.MessageName;
        var dispatched = false;
        foreach (var descriptor in descriptors)
        {
            if (!string.Equals(descriptor.MessageName, header.MessageName, StringComparison.Ordinal))
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
        await HandlerInvoker.InvokePacketAsync(descriptor, message, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask<object?> InvokeRequestAsync(
        ZLinkSpotDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        return await HandlerInvoker.InvokeRequestAsync(descriptor, message, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask InvokeSubscriptionAsync(
        ZLinkSpotSubscriptionDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        await HandlerInvoker.InvokeSubscriptionAsync(descriptor, message, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask InvokeTimerAsync(
        ZLinkSpotTimerDescriptor descriptor,
        CancellationToken cancellationToken)
    {
        await HandlerInvoker.InvokeTimerAsync(descriptor, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask<object?> InvokeActorJoinAsync(
        ZLinkSpotActorJoinDescriptor descriptor,
        IZLinkActor actor,
        object request,
        CancellationToken cancellationToken)
    {
        return await HandlerInvoker.InvokeActorJoinAsync(descriptor, actor, request, cancellationToken)
            .ConfigureAwait(false);
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
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body)
    {
        public IZLinkActor Actor { get; } = actor;

        public ZLinkActorRuntimeState RuntimeState { get; } = runtimeState;

        public ZlinkStreamHeader Header { get; } = header;

        public Message Body { get; } = body;
    }
}
