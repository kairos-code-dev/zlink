using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActivation : ZLinkSpotRuntimeContext, IAsyncDisposable
{
    private readonly AsyncServiceScope _scope;
    private readonly SemaphoreSlim _executionGate = new(1, 1);
    private readonly object _dispatchQueueGate = new();
    private readonly CancellationTokenSource _stopSource = new();
    private readonly Dictionary<string, List<ZLinkSpotSubscriptionDescriptor>> _subscriptionsByTopic = new(StringComparer.Ordinal);
    private readonly Dictionary<string, ZLinkSpotDescriptor> _packetsByName = new(StringComparer.Ordinal);
    private readonly Dictionary<Type, ZLinkSpotActorJoinDescriptor> _actorJoinsByRequestType = [];
    private readonly ZLinkSpotHandlerInvoker _handlerInvoker;
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
        _handlerInvoker = new ZLinkSpotHandlerInvoker(scope.ServiceProvider, spot);
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

    public ValueTask<IZLinkTimer> AddTimerAsync<THandler>(
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
        return ValueTask.FromResult<IZLinkTimer>(timer);
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
        IZLinkActorContext context,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        global::Zlink.Message body,
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
                        await state.Actor.OnDispatchAsync(
                            state.Context,
                            state.Header,
                            currentBody.Move(),
                            ct);
                    }
                    finally
                    {
                        state.RuntimeState.CurrentDispatch = previousDispatch;
                    }
                },
                new ActorDispatchState(actor, context, runtimeState, header, ownedBody),
                cancellationToken);
        }
        catch
        {
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
        await _handlerInvoker.InvokePacketAsync(descriptor, message, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask<object?> InvokeRequestAsync(
        ZLinkSpotDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        return await _handlerInvoker.InvokeRequestAsync(descriptor, message, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask InvokeSubscriptionAsync(
        ZLinkSpotSubscriptionDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        await _handlerInvoker.InvokeSubscriptionAsync(descriptor, message, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask InvokeTimerAsync(
        ZLinkSpotTimerDescriptor descriptor,
        CancellationToken cancellationToken)
    {
        await _handlerInvoker.InvokeTimerAsync(descriptor, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask<object?> InvokeActorJoinAsync(
        ZLinkSpotActorJoinDescriptor descriptor,
        IZLinkActor actor,
        object request,
        CancellationToken cancellationToken)
    {
        return await _handlerInvoker.InvokeActorJoinAsync(descriptor, actor, request, cancellationToken)
            .ConfigureAwait(false);
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
        Type expectedActorType,
        Type expectedRequestType,
        Type expectedReplyType)
    {
        foreach (var implemented in handlerType.GetInterfaces())
        {
            if (!implemented.IsGenericType
                || implemented.GetGenericTypeDefinition() != typeof(IZLinkSpotActorJoinHandler<,,,>))
            {
                continue;
            }

            var arguments = implemented.GetGenericArguments();
            ValidateSpotType(handlerType, expectedSpotType, arguments[0]);
            ValidateActorType(handlerType, expectedActorType, arguments[1]);

            if (arguments[2] != expectedRequestType || arguments[3] != expectedReplyType)
            {
                throw new InvalidOperationException(
                    $"SPOT actor join handler '{handlerType}' targets '{arguments[2]}/{arguments[3]}', but registration expects '{expectedRequestType}/{expectedReplyType}'.");
            }

            return new ZLinkSpotActorJoinDescriptor
            {
                HandlerType = handlerType,
                SpotType = arguments[0],
                ActorType = arguments[1],
                RequestType = arguments[2],
                ReplyType = arguments[3],
                HandleMethod = handlerType.GetMethod("HandleAsync")!,
                PacketName = ZLinkPacketNameResolver.ResolveFromType(arguments[2]),
            };
        }

        throw new InvalidOperationException(
            $"SPOT actor join handler '{handlerType}' must implement IZLinkSpotActorJoinHandler<,,,>.");
    }

    private static void ValidateSpotType(Type handlerType, Type expectedSpotType, Type actualSpotType)
    {
        if (actualSpotType != expectedSpotType)
        {
            throw new InvalidOperationException(
                $"SPOT handler '{handlerType}' targets '{actualSpotType}', but the runtime spot type is '{expectedSpotType}'.");
        }
    }

    private static void ValidateActorType(Type handlerType, Type expectedActorType, Type actualActorType)
    {
        if (actualActorType != expectedActorType)
        {
            throw new InvalidOperationException(
                $"SPOT actor join handler '{handlerType}' targets actor '{actualActorType}', but registration expects '{expectedActorType}'.");
        }
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
        IZLinkActorContext context,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        global::Zlink.Message body)
    {
        public IZLinkActor Actor { get; } = actor;

        public IZLinkActorContext Context { get; } = context;

        public ZLinkActorRuntimeState RuntimeState { get; } = runtimeState;

        public ZlinkStreamHeader Header { get; } = header;

        public global::Zlink.Message Body { get; } = body;
    }
}
