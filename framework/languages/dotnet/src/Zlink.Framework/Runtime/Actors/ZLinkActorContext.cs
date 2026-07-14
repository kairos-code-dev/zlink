namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorContext(
    ZLinkFrameworkRuntime runtime,
    ZLinkActorRuntimeState state,
    IZLinkBoundSessionFactory boundSessionFactory) : IZLinkActorContext
{
    private IZLinkActor CurrentActor
        => state.Actor ?? throw new InvalidOperationException(
            $"Actor '{state.ActorId}' has not been created.");

    public RoutingId? SpotRid => state.SpotRid;

    public IZLinkBoundSession BoundSession
    {
        get
        {
            state.EnsureContextValid();
            return boundSessionFactory.Create(state.ActorId);
        }
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
}

internal sealed class ZLinkActorJoinSpotCall(
    ZLinkFrameworkRuntime runtime,
    IZLinkActor actor,
    RoutingId spotRid,
    ZLinkMessage request) : IZLinkActorJoinSpotCall
{
    private readonly ZLinkSerialTurn? _turn = ZLinkSerialTurn.Current;
    private TimeSpan? _timeout;

    public IZLinkActorJoinSpotCall Timeout(TimeSpan timeout)
    {
        ZLinkRequestTimeoutValidation.Validate(timeout, nameof(timeout));
        _timeout = timeout;
        return this;
    }

    public ValueTask<ZLinkActorJoinResult> Async(CancellationToken cancellationToken = default)
    {
        return ExecuteAsync(cancellationToken);
    }

    public ValueTask<ZLinkActorJoinResult> Yield(CancellationToken cancellationToken = default)
    {
        return _turn is null
            ? ExecuteAsync(cancellationToken)
            : _turn.YieldFrameworkCallAsync(ExecuteAsync, cancellationToken);
    }

    private async ValueTask<ZLinkActorJoinResult> ExecuteAsync(CancellationToken cancellationToken)
    {
        using var operation = runtime.EnterOperation();
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            runtime.Flow.CaptureEnabled);
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
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested &&
                                                 timeoutSource.IsCancellationRequested)
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
    private readonly ZLinkSerialTurn? _turn = ZLinkSerialTurn.Current;
    private TimeSpan? _timeout;

    public IZLinkActorJoinEntrySpotCall Timeout(TimeSpan timeout)
    {
        ZLinkRequestTimeoutValidation.Validate(timeout, nameof(timeout));
        _timeout = timeout;
        return this;
    }

    public ValueTask<ZLinkActorJoinResult> Async(CancellationToken cancellationToken = default)
    {
        return ExecuteAsync(cancellationToken);
    }

    public ValueTask<ZLinkActorJoinResult> Yield(CancellationToken cancellationToken = default)
    {
        return _turn is null
            ? ExecuteAsync(cancellationToken)
            : _turn.YieldFrameworkCallAsync(ExecuteAsync, cancellationToken);
    }

    private async ValueTask<ZLinkActorJoinResult> ExecuteAsync(CancellationToken cancellationToken)
    {
        using var operation = runtime.EnterOperation();
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
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested &&
                                                 timeoutSource.IsCancellationRequested)
        {
            throw new TimeoutException($"Entry SPOT actor join timed out after {timeout}.");
        }
    }

}
