namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorContext(
    ZLinkFrameworkRuntime runtime,
    ZLinkActorRuntimeState state,
    IZLinkBoundSessionService boundSessionService) : IZLinkActorContext
{
    private IZLinkActor CurrentActor
        => state.Actor ?? throw new InvalidOperationException(
            $"Actor '{state.ActorId}' has not been created.");

    public string MeshName
        => ZLinkActorDrainCoordinator.ResolveMeshName(
               runtime.Registration,
               state.ActorType ?? throw new InvalidOperationException(
                   $"Actor '{state.ActorId}' does not have a registered actor type."))
           ?? throw new InvalidOperationException(
               $"Actor '{state.ActorId}' does not belong to a registered RouteMesh.");

    public string? SpotId => state.SpotId;

    public IZLinkBoundSession BoundSession
    {
        get
        {
            state.EnsureContextValid();
            return boundSessionService.Create(state.ActorId);
        }
    }

    public IZLinkActorJoinSpotCall JoinSpot(
        string spotId,
        ZLinkMessage request)
    {
        state.EnsureContextValid();
        ArgumentNullException.ThrowIfNull(request);
        return ZLinkActorJoinCall.ForSpot(
            runtime,
            CurrentActor,
            spotId,
            request);
    }

    public IZLinkActorJoinEntrySpotCall JoinEntrySpot(ZLinkMessage request)
    {
        state.EnsureContextValid();
        ArgumentNullException.ThrowIfNull(request);
        return ZLinkActorJoinCall.ForEntrySpot(
            runtime,
            CurrentActor,
            state.NativeActorRef?.NodeRid
            ?? throw new InvalidOperationException(
                $"Actor '{state.ActorId}' does not have a current node identity."),
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
    private readonly RoutingId _targetNodeRid;
    private readonly string? _targetSpotId;
    private readonly TargetKind _targetKind;
    private readonly ZLinkSerialTurn? _turn;
    private TimeSpan? _timeout;

    private ZLinkActorJoinCall(
        ZLinkFrameworkRuntime runtime,
        IZLinkActor actor,
        RoutingId targetNodeRid,
        string? targetSpotId,
        ZLinkMessage request,
        TargetKind targetKind)
    {
        _runtime = runtime;
        _actor = actor;
        _targetNodeRid = targetNodeRid;
        _targetSpotId = targetSpotId;
        _request = request;
        _targetKind = targetKind;
        _turn = ZLinkSerialTurn.Current;
    }

    public static IZLinkActorJoinSpotCall ForSpot(
        ZLinkFrameworkRuntime runtime,
        IZLinkActor actor,
        string spotId,
        ZLinkMessage request)
    {
        return new ZLinkActorJoinCall(
            runtime, actor, default, ZLinkSpotId.Require(spotId, nameof(spotId)),
            request, TargetKind.Spot);
    }

    public static IZLinkActorJoinEntrySpotCall ForEntrySpot(
        ZLinkFrameworkRuntime runtime,
        IZLinkActor actor,
        RoutingId spotNodeRid,
        ZLinkMessage request)
    {
        return new ZLinkActorJoinCall(
            runtime, actor, spotNodeRid, null, request, TargetKind.EntrySpot);
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
                    _targetSpotId!,
                    _actor,
                    _request,
                    timeoutSource.Token).ConfigureAwait(false)
                : await _runtime.JoinActorEntrySpotAsync(
                    _targetNodeRid,
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

    private enum TargetKind
    {
        Spot,
        EntrySpot
    }
}
