namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorContext(
    ZLinkFrameworkRuntime runtime,
    ZLinkActorRuntimeState state) : IZLinkActorContext
{
    public string ActorId => state.ActorId;

    public string? SessionId => state.SessionId;

    public string? SpotName => state.SpotName;

    public RoutingId? SpotRid => state.SpotRid;

    public bool IsJoined => state.IsJoined;

    public IZLinkSessionProxy SessionProxy
        => ((IZLinkSessionProxyFactory?)runtime.Services.GetService(typeof(IZLinkSessionProxyFactory))
            ?? throw new InvalidOperationException("Session proxy factory is not registered."))
            .Create(state.ActorId);

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
        return state.GetJoinedSpot();
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
        RoutingId spotRid,
        TRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        return new ZLinkActorJoinSpotCall<TRequest>(
            runtime,
            CurrentActor,
            null,
            spotRid,
            request);
    }

    public ValueTask<TReply> JoinSpotAsync<TRequest, TReply>(
        string spotName,
        TRequest request,
        CancellationToken cancellationToken = default)
    {
        return JoinSpot(spotName, request).SubmitAsync<TReply>(cancellationToken);
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

internal sealed class ZLinkActorJoinSpotCall<TRequest>(
    ZLinkFrameworkRuntime runtime,
    IZLinkActor actor,
    string? spotName,
    RoutingId? spotRid,
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
            var resolvedSpotRid = await ResolveSpotRidAsync(timeoutSource.Token).ConfigureAwait(false);
            return await runtime.JoinActorAsync<TRequest, TReply>(
                resolvedSpotRid,
                actor,
                request,
                timeoutSource.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested && timeoutSource.IsCancellationRequested)
        {
            throw new TimeoutException($"SPOT actor join timed out after {timeout}.");
        }
    }

    private async ValueTask<RoutingId> ResolveSpotRidAsync(CancellationToken cancellationToken)
    {
        if (spotRid is { } rid)
        {
            return rid;
        }

        if (runtime.Services.GetService(typeof(IZLinkSpotRemoteAddressResolver)) is not IZLinkSpotRemoteAddressResolver resolver)
        {
            throw new ZLinkConfigurationException(
                "Actor JoinSpot(string, ...) requires AddSpotRemoteAddressResolver<TResolver>().");
        }

        var route = await resolver.ResolveSpotRemoteAddressAsync(
            spotName ?? throw new InvalidOperationException("SPOT name is required."),
            cancellationToken).ConfigureAwait(false);
        return route.SpotRid;
    }
}
