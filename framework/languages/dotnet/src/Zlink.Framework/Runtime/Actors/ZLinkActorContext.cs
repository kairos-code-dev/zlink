namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorContext(
    ZLinkFrameworkRuntime runtime,
    ZLinkActorRuntimeState state) : IZLinkActorContext
{
    public RoutingId? SpotRid => state.SpotRid;

    public bool IsJoined => state.IsJoined;

    public IZLinkBoundSession BoundSession
    {
        get
        {
            state.EnsureContextValid();
            return ((IZLinkBoundSessionFactory?)runtime.Services.GetService(typeof(IZLinkBoundSessionFactory))
                    ?? throw new InvalidOperationException("Bound session factory is not registered."))
                .Create(state.ActorId);
        }
    }

    public IZLinkSpot GetSpot()
    {
        state.EnsureContextValid();
        return state.GetJoinedSpot();
    }

    public TSpot GetSpot<TSpot>()
        where TSpot : IZLinkSpot
    {
        state.EnsureContextValid();
        var spot = GetSpot();
        if (spot is not TSpot typed)
        {
            throw new InvalidOperationException(
                $"Actor joined SPOT '{spot.GetType()}', not '{typeof(TSpot)}'.");
        }

        return typed;
    }

    public IZLinkActorJoinSpotCall JoinSpot(
        RoutingId spotRid,
        ZLinkMessage request)
    {
        state.EnsureContextValid();
        ArgumentNullException.ThrowIfNull(request);
        return new ZLinkActorJoinSpotCall(
            runtime,
            CurrentActor,
            spotRid,
            request);
    }

    public IZLinkActorJoinEntrySpotCall JoinEntrySpot(RoutingId spotNodeRid, ZLinkMessage request)
    {
        state.EnsureContextValid();
        ArgumentNullException.ThrowIfNull(request);
        return new ZLinkActorJoinEntrySpotCall(
            runtime,
            CurrentActor,
            spotNodeRid,
            request);
    }

    private IZLinkActor CurrentActor
        => state.Actor ?? throw new InvalidOperationException(
            $"Actor '{state.ActorId}' has not been created.");
}

internal sealed class ZLinkActorJoinSpotCall(
    ZLinkFrameworkRuntime runtime,
    IZLinkActor actor,
    RoutingId spotRid,
    ZLinkMessage request) : IZLinkActorJoinSpotCall
{
    private TimeSpan? _timeout;

    public IZLinkActorJoinSpotCall Timeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public async ValueTask<ZLinkActorJoinResult> Async(
        CancellationToken cancellationToken = default)
    {
        var timeout = _timeout ?? runtime.Registration.DefaultRequestTimeout;
        using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(timeout);

        try
        {
            return await runtime.JoinActorAsync(
                spotRid,
                actor,
                request,
                timeoutSource.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested && timeoutSource.IsCancellationRequested)
        {
            throw new TimeoutException($"SPOT actor join timed out after {timeout}.");
        }
    }
}

internal sealed class ZLinkActorJoinEntrySpotCall(
    ZLinkFrameworkRuntime runtime,
    IZLinkActor actor,
    RoutingId spotNodeRid,
    ZLinkMessage request) : IZLinkActorJoinEntrySpotCall
{
    private TimeSpan? _timeout;

    public IZLinkActorJoinEntrySpotCall Timeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public async ValueTask<ZLinkActorJoinResult> Async(CancellationToken cancellationToken = default)
    {
        var timeout = _timeout ?? runtime.Registration.DefaultRequestTimeout;
        using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(timeout);

        try
        {
            return await runtime.JoinActorEntrySpotAsync(
                spotNodeRid,
                actor,
                request,
                timeoutSource.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested && timeoutSource.IsCancellationRequested)
        {
            throw new TimeoutException($"Entry SPOT actor join timed out after {timeout}.");
        }
    }
}
