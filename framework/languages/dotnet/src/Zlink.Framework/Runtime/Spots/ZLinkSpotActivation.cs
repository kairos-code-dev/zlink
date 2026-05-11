using Microsoft.Extensions.DependencyInjection;
using Systems.Zlink.Stream.Connector.Protocol;
using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActivation : IZLinkSpotContext, IAsyncDisposable
{
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly AsyncServiceScope _scope;
    private readonly CancellationTokenSource _stopSource = new();
    private readonly ZLinkSpotSerialExecutor _serial;
    private readonly ZLinkSpotPacketRegistry _packets = new();
    private readonly ZLinkSpotActorJoinRegistry _actorJoins = new();
    private readonly ZLinkSpotActorMembership _actors = new();
    private readonly ZLinkSpotSubscriptionRegistry _subscriptions = new();
    private ZLinkSpotHandlerInvoker? _handlerInvoker;
    private IZLinkSpot? _spot;
    private readonly ZLinkSpotTimerRegistry _timers = new();
    private readonly TimeSpan _defaultTimeout;
    private readonly ZLinkAsyncSubmitter _submitter;
    private static readonly ZlinkStreamHeaderCodec HeaderCodec = new();
    private static readonly TimeSpan SubscriptionPollInterval = TimeSpan.FromMilliseconds(20);
    private Task? _subscriptionPump;
    private int _disposed;
    private bool _configurationOpen = true;

    public ZLinkSpotActivation(
        ZLinkFrameworkRuntime runtime,
        AsyncServiceScope scope,
        IZLinkBackendSpot nativeSpot,
        RoutingId nodeRid,
        string spotName,
        string channelName,
        TimeSpan defaultTimeout,
        TimeSpan? sendTimeout)
    {
        _runtime = runtime;
        _scope = scope;
        NativeSpot = nativeSpot;
        NodeRid = nodeRid;
        SpotName = spotName;
        ChannelName = channelName;
        _defaultTimeout = defaultTimeout;
        _submitter = new ZLinkAsyncSubmitter(
            NativeSpot.OnSendReady,
            sendTimeout,
            _stopSource.Token);
        _serial = new ZLinkSpotSerialExecutor(this, () => IsDisposed, _stopSource.Token);
    }

    public IZLinkSpot Spot => _spot
        ?? throw new InvalidOperationException("SPOT has not been attached to this context.");

    private ZLinkSpotHandlerInvoker HandlerInvoker => _handlerInvoker
        ?? throw new InvalidOperationException("SPOT has not been attached to this context.");

    public IZLinkBackendSpot NativeSpot { get; }

    public string SpotName { get; }

    public string ChannelName { get; }

    public TimeSpan DefaultTimeout => _defaultTimeout;

    public RoutingId SpotRid => NativeSpot.RoutingId;

    public RoutingId NodeRid { get; }

    public int SubscriptionMessageCount => _subscriptions.MessageCount;

    public int SubscriptionDispatchCount => _subscriptions.DispatchCount;

    public int SubscriptionIgnoreCount => _subscriptions.IgnoreCount;

    public string? LastSubscriptionTopic => _subscriptions.LastTopic;

    public string? LastSubscriptionMessageName => _subscriptions.LastMessageName;

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

        _packets.Bind(Spot);
        _subscriptions.Bind(Spot, NativeSpot);
        _actorJoins.Bind(Spot);
    }

    public void AddPacket<THandler>()
        where THandler : class
    {
        EnsureConfigurationOpen();
        _packets.Add(typeof(THandler));
    }

    public void AddSubscribe<THandler>(string topic)
        where THandler : class
    {
        EnsureConfigurationOpen();
        _subscriptions.Add(topic, typeof(THandler));
    }

    public void AddActorJoin<THandler, TActor, TRequest, TReply>()
        where THandler : class
        where TActor : IZLinkActor
    {
        EnsureConfigurationOpen();
        _actorJoins.Add(
            typeof(THandler),
            typeof(TActor),
            typeof(TRequest),
            typeof(TReply));
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
        RegisterWithoutSynchronizationContext(() =>
        {
            NativeSpot.OnDispatchEvent(info =>
            {
                if (info.Event == ZLinkBackendSpotDispatchEvent.RoutedReadable)
                {
                    QueueSerialized(
                        static (activation, ct) => activation.DispatchRoutedDrainAsync(ct));
                }
                else if (info.Event == ZLinkBackendSpotDispatchEvent.ChannelReplyReadable)
                {
                    info.DrainChannelReply?.Invoke();
                }
                else if (info.Event == ZLinkBackendSpotDispatchEvent.ActorJoinReadable)
                {
                    QueueSerialized(
                        static (activation, ct) => activation.DispatchActorJoinDrainAsync(ct));
                }
                else if (info.Event == ZLinkBackendSpotDispatchEvent.ActorReadable
                    && info.ActorParts is { Count: > 0 } actorParts)
                {
                    QueueSerialized(
                        static (activation, state, ct) => activation.DispatchActorPartsAsync(state, ct),
                        actorParts);
                }
            });

            return 0;
        });

        if (_subscriptions.HasSubscriptions)
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
        return _timers.AddAsync(
            name,
            period,
            typeof(THandler),
            Spot.GetType(),
            StopToken,
            (descriptor, ct) => ExecuteSerializedAsync(
                async static (activation, state, innerCt) =>
                {
                    await activation.InvokeTimerAsync(state, innerCt);
                },
                descriptor,
                ct),
            cancellationToken);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestChannelAsync(
        string channelName,
        Message message,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        var requestTimeout = timeout ?? _defaultTimeout;
        return await _submitter
            .SubmitRequestAsync<IReadOnlyList<Message>>(
                message,
                (pending, complete, fail) => NativeSpot.RequestChannel(
                    channelName,
                    pending,
                    (result, reply) =>
                    {
                        if (result == RequestResult.Ok)
                        {
                            complete(reply);
                            return;
                        }

                        foreach (var replyPart in reply)
                        {
                            replyPart.Dispose();
                        }

                        fail(new TimeoutException($"SPOT channel request failed with result '{result}'."));
                    },
                    SendFlags.DontWait,
                    requestTimeout),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask SendChannelAsync(
        string channelName,
        Message message,
        CancellationToken cancellationToken)
    {
        return _submitter.SubmitAsync(
            message,
            pending => NativeSpot.SendChannel(channelName, pending, SendFlags.DontWait),
            cancellationToken);
    }

    public ValueTask PublishCurrentAsync(
        string topic,
        Message message,
        CancellationToken cancellationToken)
    {
        return _submitter.SubmitAsync(
            message,
            pending => NativeSpot.Publish(topic, pending, SendFlags.DontWait),
            cancellationToken);
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

    public bool TryResolveActorJoinDescriptor(
        Type requestType,
        out ZLinkSpotActorJoinDescriptor? descriptor)
    {
        return _actorJoins.TryResolve(requestType, out descriptor);
    }

    public async ValueTask<TReply> JoinActorAsync<TRequest, TReply>(
        IZLinkActor actor,
        TRequest request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);

        if (!_actorJoins.TryResolve(typeof(TRequest), out var descriptor)
            || descriptor is null)
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
        _actors.Add(SpotName, actor);
        await _runtime.JoinActorToSpotAsync(this, actor, cancellationToken);
    }

    private async ValueTask LeaveActorCoreAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        _actors.RemoveIfCurrent(actor);
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

        await _serial.DisposeAsync();
        await _timers.DisposeAsync();
        await _submitter.DisposeAsync();
        await NativeSpot.DisposeAsync();
        _stopSource.Dispose();
        await _scope.DisposeAsync();
    }

    private async ValueTask ExecuteSerializedAsync(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        await _serial.ExecuteAsync(operation, cancellationToken).ConfigureAwait(false);
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
        await _serial.ExecuteAsync(operation, state, cancellationToken).ConfigureAwait(false);
    }

    private void QueueSerialized(Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation)
    {
        _serial.Queue(operation);
    }

    private void QueueSerialized<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state)
    {
        var capturedOp = operation;
        var capturedState = state;
        _serial.Queue((activation, ct) => capturedOp(activation, capturedState, ct));
    }

    private async ValueTask DispatchActorJoinDrainAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            ZLinkBackendActorJoinRequest? request;
            try
            {
                request = NativeSpot.RecvActorJoin(RecvFlags.DontWait);
            }
            catch (ZlinkRecvException ex)
                when (ex.Result == ZlinkRecvException.ErrorCode.NoData)
            {
                return;
            }

            if (request is null)
            {
                return;
            }

            await DispatchActorJoinAsync(request, cancellationToken).ConfigureAwait(false);
        }
    }

    private async ValueTask DispatchActorJoinAsync(
        ZLinkBackendActorJoinRequest joinRequest,
        CancellationToken cancellationToken)
    {
        var header = ZLinkEnvelopeCodec.DecodeHeader(joinRequest.Message);
        if (!_actorJoins.TryResolveByName(header.MessageName, out var descriptor) || descriptor is null)
        {
            using var emptyReply = Message.FromBytes(ReadOnlySpan<byte>.Empty);
            NativeSpot.ReplyActorJoin(joinRequest, accepted: false, emptyReply);
            return;
        }

        if (!_actors.TryGetActor(joinRequest.TargetActor.ActorId, out var actor) || actor is null)
        {
            using var emptyReply = Message.FromBytes(ReadOnlySpan<byte>.Empty);
            NativeSpot.ReplyActorJoin(joinRequest, accepted: false, emptyReply);
            return;
        }

        var requestObj = ZLinkEnvelopeCodec.DecodeBody(joinRequest.Message, descriptor.RequestType)!;
        var replyObj = await InvokeActorJoinAsync(descriptor, actor, requestObj, cancellationToken)
            .ConfigureAwait(false);

        var replyEnvelopeHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Response,
            ChannelName,
            descriptor.MessageName,
            ZLinkEnvelopeCodec.DefaultContentType,
            null, null, null, null, null);
        using var replyMessage = ZLinkEnvelopeCodec.Encode(replyEnvelopeHeader, replyObj, descriptor.ReplyType);
        NativeSpot.ReplyActorJoin(joinRequest, accepted: true, replyMessage);
    }

    private async ValueTask DispatchActorPartsAsync(
        IReadOnlyList<ZLinkBackendActorPart> parts,
        CancellationToken cancellationToken)
    {
        int i = 0;
        while (i < parts.Count)
        {
            var headerPart = parts[i++];
            if (!_actors.TryGetActor(headerPart.Actor.ActorId, out var actor) || actor is null)
            {
                headerPart.Message.Dispose();
                while (i < parts.Count && parts[i - 1].More)
                {
                    parts[i++].Message.Dispose();
                }
                continue;
            }

            if (!headerPart.More)
            {
                await DispatchActorStreamPartAsync(
                    actor,
                    headerPart.Actor.ActorId,
                    HeaderCodec.Decode(headerPart.Message.AsReadOnlyMemory()),
                    Message.FromBytes(ReadOnlySpan<byte>.Empty),
                    cancellationToken).ConfigureAwait(false);
                headerPart.Message.Dispose();
                continue;
            }

            if (i >= parts.Count)
            {
                headerPart.Message.Dispose();
                continue;
            }

            var bodyPart = parts[i++];
            var streamHeader = HeaderCodec.Decode(headerPart.Message.AsReadOnlyMemory());
            headerPart.Message.Dispose();
            using var body = bodyPart.Message;
            await DispatchActorStreamPartAsync(actor, headerPart.Actor.ActorId, streamHeader, body, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private async ValueTask DispatchActorStreamPartAsync(
        IZLinkActor actor,
        string actorId,
        ZlinkStreamHeader streamHeader,
        Message body,
        CancellationToken cancellationToken)
    {
        var runtimeState = _runtime.GetOrCreateActorState(actorId);
        var previousDispatch = runtimeState.CurrentDispatch;
        runtimeState.CurrentDispatch = new ZLinkActorDispatchState(streamHeader);
        try
        {
            await runtimeState.DispatchAsync(_runtime.Services, actor, streamHeader, body, cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            runtimeState.CurrentDispatch = previousDispatch;
        }
    }

    private async ValueTask DispatchRoutedAsync(Received received, CancellationToken cancellationToken)
    {
        using (received)
        {
            if (received.Parts.Count == 0)
            {
                return;
            }

            var header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts[0]);
            if (!_packets.TryResolve(header, out var descriptor)
                || descriptor is null)
            {
                return;
            }

            var message = ZLinkEnvelopeCodec.DecodeBody(received.Parts[0], descriptor.MessageType);
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
            catch (ZlinkRecvException ex)
                when (ex.Result == ZlinkRecvException.ErrorCode.NoData)
            {
                return;
            }

            await DispatchRoutedAsync(received, cancellationToken);
        }
    }

    private async ValueTask DispatchSubscriptionsAsync(CancellationToken cancellationToken)
    {
        await _subscriptions
            .DrainAsync(NativeSpot, InvokeSubscriptionAsync, cancellationToken)
            .ConfigureAwait(false);
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
            catch (ZlinkRecvException ex)
                when (cancellationToken.IsCancellationRequested
                      || ex.Result is ZlinkRecvException.ErrorCode.InternalError
                      or ZlinkRecvException.ErrorCode.InvalidHandle)
            {
                return;
            }
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
