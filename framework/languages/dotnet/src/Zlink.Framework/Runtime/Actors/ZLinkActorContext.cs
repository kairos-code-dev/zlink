namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorContext(
    ZLinkFrameworkRuntime runtime,
    ZLinkActorRuntimeState state) : IZLinkActorContext
{
    public string ActorId => state.ActorId;

    public string? SessionId => state.SessionId;

    public string? SpotName => state.Activation is { IsDisposed: false } activation
        ? activation.SpotName
        : null;

    public RoutingId? SpotRid => state.Activation is { IsDisposed: false } activation
        ? activation.SpotRid
        : null;

    public bool IsJoined => state.Activation is not null && !state.Activation.IsDisposed;

    public void AddPacket<THandler>()
        where THandler : class
    {
        state.AddPacket(CurrentActor, typeof(THandler), null);
    }

    public void AddPacket<THandler>(string messageName)
        where THandler : class
    {
        if (string.IsNullOrWhiteSpace(messageName))
        {
            throw new InvalidOperationException("Actor packet name must not be empty.");
        }

        state.AddPacket(CurrentActor, typeof(THandler), messageName);
    }

    public IZLinkSpot GetSpot()
    {
        return state.Activation is { IsDisposed: false } activation
            ? activation.Spot
            : throw new InvalidOperationException("Actor has not joined a SPOT.");
    }

    public TSpot GetSpot<TSpot>()
        where TSpot : IZLinkSpot
    {
        var spot = GetSpot();
        if (spot is not TSpot typed)
        {
            throw new InvalidOperationException(
                $"Actor joined SPOT '{spot.GetType()}', not '{typeof(TSpot)}'.");
        }

        return typed;
    }

    public IZLinkActorJoinSpotCall JoinSpot<TRequest>(
        string spotName,
        TRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (string.IsNullOrWhiteSpace(spotName))
        {
            throw new InvalidOperationException("SPOT name must not be empty.");
        }

        return new ZLinkActorJoinSpotCall<TRequest>(runtime, CurrentActor, spotName, null, request);
    }

    public IZLinkActorJoinSpotCall JoinSpot<TRequest>(
        ZLinkSpotId spotId,
        TRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        return new ZLinkActorJoinSpotCall<TRequest>(runtime, CurrentActor, null, spotId, request);
    }

    public IZLinkActorJoinSpotCall JoinSpot<TRequest>(
        RoutingId spotRid,
        TRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        return new ZLinkActorJoinSpotCall<TRequest>(
            runtime,
            CurrentActor,
            null,
            ZLinkSpotId.FromRoutingId(spotRid),
            request);
    }

    public IZLinkRequestCall RequestChannel<TRequest>(
        string channelName,
        TRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        return new ZLinkActorChannelRequestCall<TRequest>(runtime, state, channelName, request);
    }

    public IZLinkSendCall SendChannel<TMessage>(
        string channelName,
        TMessage message)
    {
        return new ZLinkActorChannelSendCall<TMessage>(runtime, state, channelName, message);
    }

    public IZLinkActorSendCall Send<TMessage>(TMessage message)
    {
        return new ZLinkActorSendCall<TMessage>(state, message);
    }

    public IZLinkActorReplyCall Reply<TMessage>(TMessage message)
    {
        return new ZLinkActorReplyCall<TMessage>(state, message);
    }

    public ValueTask<TReply> JoinSpotAsync<TRequest, TReply>(
        string spotName,
        TRequest request,
        CancellationToken cancellationToken = default)
    {
        return JoinSpot(spotName, request).SubmitAsync<TReply>(cancellationToken);
    }

    public ValueTask<TReply> JoinSpotAsync<TRequest, TReply>(
        ZLinkSpotId spotId,
        TRequest request,
        CancellationToken cancellationToken = default)
    {
        return JoinSpot(spotId, request).SubmitAsync<TReply>(cancellationToken);
    }

    public ValueTask<TReply> JoinSpotAsync<TRequest, TReply>(
        RoutingId spotRid,
        TRequest request,
        CancellationToken cancellationToken = default)
    {
        return JoinSpot(spotRid, request).SubmitAsync<TReply>(cancellationToken);
    }

    private IZLinkActor CurrentActor
        => state.Actor ?? throw new InvalidOperationException(
            $"Actor '{state.ActorId}' has not been created.");
}

internal sealed class ZLinkActorChannelSendCall<TMessage>(
    ZLinkFrameworkRuntime runtime,
    ZLinkActorRuntimeState state,
    string channelName,
    TMessage message) : IZLinkSendCall
{
    private string? _messageName;
    private bool _hasMessageName;

    public IZLinkSendCall PacketName(string messageName)
    {
        _messageName = messageName;
        _hasMessageName = true;
        return this;
    }

    public ValueTask Submit(CancellationToken cancellationToken = default)
    {
        IZLinkSendCall inner = state.Activation is { IsDisposed: false } activation
            ? new ZLinkCurrentSpotSendCall<TMessage>(activation, channelName, message)
            : new ZLinkSendCall(runtime, runtime.Registration, channelName, message);

        if (_hasMessageName)
        {
            inner.PacketName(_messageName ?? string.Empty);
        }

        return inner.Submit(cancellationToken);
    }
}

internal sealed class ZLinkActorChannelRequestCall<TRequest>(
    ZLinkFrameworkRuntime runtime,
    ZLinkActorRuntimeState state,
    string channelName,
    TRequest request) : IZLinkRequestCall
{
    private string? _messageName;
    private bool _hasMessageName;
    private TimeSpan? _timeout;

    public IZLinkRequestCall PacketName(string messageName)
    {
        _messageName = messageName;
        _hasMessageName = true;
        return this;
    }

    public IZLinkRequestCall Timeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default)
    {
        IZLinkRequestCall inner = state.Activation is { IsDisposed: false } activation
            ? new ZLinkCurrentSpotRequestCall<TRequest>(activation, channelName, request)
            : new ZLinkRequestCall<TRequest>(runtime, runtime.Registration, channelName, request);

        if (_hasMessageName)
        {
            inner.PacketName(_messageName ?? string.Empty);
        }

        if (_timeout is { } timeout)
        {
            inner.Timeout(timeout);
        }

        return inner.SubmitAsync<TReply>(cancellationToken);
    }
}

internal sealed class ZLinkActorJoinSpotCall<TRequest>(
    ZLinkFrameworkRuntime runtime,
    IZLinkActor actor,
    string? spotName,
    ZLinkSpotId? spotId,
    TRequest request) : IZLinkActorJoinSpotCall
{
    private TimeSpan? _timeout;

    public IZLinkActorJoinSpotCall Timeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public async ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default)
    {
        var timeout = _timeout ?? runtime.Registration.DefaultTimeout;
        using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(timeout);

        try
        {
            var resolvedSpotId = await ResolveSpotIdAsync(timeoutSource.Token).ConfigureAwait(false);
            return await runtime.JoinActorAsync<TRequest, TReply>(
                resolvedSpotId.ToRoutingId(),
                actor,
                request,
                timeoutSource.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested && timeoutSource.IsCancellationRequested)
        {
            throw new TimeoutException($"SPOT actor join timed out after {timeout}.");
        }
    }

    private async ValueTask<ZLinkSpotId> ResolveSpotIdAsync(CancellationToken cancellationToken)
    {
        if (spotId is { } id)
        {
            return id;
        }

        if (runtime.Services.GetService(typeof(IZLinkSpotRouteResolver)) is not IZLinkSpotRouteResolver resolver)
        {
            throw new ZLinkConfigurationException(
                "Actor JoinSpot(string, ...) requires AddSpotRouteResolver<TResolver>().");
        }

        var route = await resolver.ResolveSpotRouteAsync(
            spotName ?? throw new InvalidOperationException("SPOT name is required."),
            cancellationToken).ConfigureAwait(false);
        return route.SpotId;
    }
}
