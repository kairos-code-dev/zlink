namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorContext(
    ZLinkFrameworkRuntime runtime,
    ZLinkActorRuntimeState state,
    IZLinkBoundSessionService boundSessionService) : IZLinkActorContext
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
            return boundSessionService.Create(state.ActorId);
        }
    }

    public IZLinkActorJoinSpotCall JoinSpot(
        RoutingId spotRid,
        ZLinkMessage request)
    {
        state.EnsureContextValid();
        ArgumentNullException.ThrowIfNull(request);
        return ZLinkActorJoinCall.ForSpot(
            runtime,
            CurrentActor,
            spotRid,
            request);
    }

    public IZLinkActorJoinEntrySpotCall JoinEntrySpot(RoutingId spotNodeRid, ZLinkMessage request)
    {
        state.EnsureContextValid();
        ArgumentNullException.ThrowIfNull(request);
        return ZLinkActorJoinCall.ForEntrySpot(
            runtime,
            CurrentActor,
            spotNodeRid,
            request);
    }
}

internal sealed class ZLinkActorJoinCall :
    IZLinkActorJoinSpotCall,
    IZLinkActorJoinEntrySpotCall
{
    private readonly IZLinkActor _actor;
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly ZLinkMessage _request;
    private readonly RoutingId _targetRid;
    private readonly TargetKind _targetKind;
    private readonly ZLinkSerialTurn? _turn;
    private TimeSpan? _timeout;

    private ZLinkActorJoinCall(
        ZLinkFrameworkRuntime runtime,
        IZLinkActor actor,
        RoutingId targetRid,
        ZLinkMessage request,
        TargetKind targetKind)
    {
        _runtime = runtime;
        _actor = actor;
        _targetRid = targetRid;
        _request = request;
        _targetKind = targetKind;
        _turn = ZLinkSerialTurn.Current;
    }

    public static IZLinkActorJoinSpotCall ForSpot(
        ZLinkFrameworkRuntime runtime,
        IZLinkActor actor,
        RoutingId spotRid,
        ZLinkMessage request)
    {
        return new ZLinkActorJoinCall(runtime, actor, spotRid, request, TargetKind.Spot);
    }

    public static IZLinkActorJoinEntrySpotCall ForEntrySpot(
        ZLinkFrameworkRuntime runtime,
        IZLinkActor actor,
        RoutingId spotNodeRid,
        ZLinkMessage request)
    {
        return new ZLinkActorJoinCall(runtime, actor, spotNodeRid, request, TargetKind.EntrySpot);
    }

    IZLinkActorJoinSpotCall IZLinkActorJoinSpotCall.Timeout(TimeSpan timeout)
    {
        SetTimeout(timeout);
        return this;
    }

    IZLinkActorJoinEntrySpotCall IZLinkActorJoinEntrySpotCall.Timeout(TimeSpan timeout)
    {
        SetTimeout(timeout);
        return this;
    }

    public ValueTask<ZLinkActorJoinResult> Async(CancellationToken cancellationToken = default)
    {
        return ExecuteAsync(cancellationToken);
    }

    public void Submit(CancellationToken cancellationToken = default)
    {
        ZLinkUnawaitedSubmit.Observe(
            ObserveAsync(cancellationToken),
            _targetKind == TargetKind.Spot
                ? "actor Spot join submit"
                : "actor Entry Spot join submit",
            _runtime.ErrorSink);
    }

    public ValueTask<ZLinkActorJoinResult> Yield(CancellationToken cancellationToken = default)
    {
        return _turn is null
            ? ExecuteAsync(cancellationToken)
            : _turn.YieldFrameworkCallAsync(ExecuteAsync, cancellationToken);
    }

    private void SetTimeout(TimeSpan timeout)
    {
        ZLinkRequestTimeoutValidation.Validate(timeout, nameof(timeout));
        _timeout = timeout;
    }

    private async ValueTask<ZLinkActorJoinResult> ExecuteAsync(CancellationToken cancellationToken)
    {
        using var operation = _runtime.EnterOperation();
        using var flow = _targetKind == TargetKind.Spot
            ? ZLinkFlowContext.EnterCurrentOrCreate(
                ZLinkFlowOrigin.Application,
                _runtime.Flow.CaptureEnabled)
            : default;
        var timeout = _timeout ?? _runtime.Registration.DefaultRequestTimeout;
        using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(timeout);

        try
        {
            return _targetKind == TargetKind.Spot
                ? await _runtime.JoinActorAsync(
                    _targetRid,
                    _actor,
                    _request,
                    timeoutSource.Token).ConfigureAwait(false)
                : await _runtime.JoinActorEntrySpotAsync(
                    _targetRid,
                    _actor,
                    _request,
                    timeoutSource.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested &&
                                                 timeoutSource.IsCancellationRequested)
        {
            var target = _targetKind == TargetKind.Spot ? "SPOT" : "Entry SPOT";
            throw new TimeoutException($"{target} actor join timed out after {timeout}.");
        }
    }

    private async ValueTask ObserveAsync(CancellationToken cancellationToken)
    {
        _ = await ExecuteAsync(cancellationToken).ConfigureAwait(false);
    }

    private enum TargetKind
    {
        Spot,
        EntrySpot
    }
}
