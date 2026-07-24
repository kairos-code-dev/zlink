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

    public string ActorId => state.ActorId;

    public ulong ObjectGeneration
        => state.NativeActorRef?.Generation
           ?? throw new InvalidOperationException(
               $"Actor '{state.ActorId}' does not have an object generation.");

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
    private readonly string? _targetSpotId;
    private int _submitted;
    private TimeSpan? _timeout;

    private ZLinkActorJoinCall(
        ZLinkFrameworkRuntime runtime,
        IZLinkActor actor,
        string? targetSpotId,
        ZLinkMessage request)
    {
        _runtime = runtime;
        _actor = actor;
        _targetSpotId = targetSpotId;
        _request = request;
    }

    public static IZLinkActorJoinSpotCall ForSpot(
        ZLinkFrameworkRuntime runtime,
        IZLinkActor actor,
        string spotId,
        ZLinkMessage request)
    {
        return new ZLinkActorJoinCall(
            runtime, actor, ZLinkSpotId.Require(spotId, nameof(spotId)), request);
    }

    public static IZLinkActorJoinEntrySpotCall ForEntrySpot(
        ZLinkFrameworkRuntime runtime,
        IZLinkActor actor,
        ZLinkMessage request)
    {
        return new ZLinkActorJoinCall(runtime, actor, null, request);
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

    public void Defer()
    {
        if (Interlocked.Exchange(ref _submitted, 1) != 0)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.AlreadySubmitted,
                "Actor Join was already deferred.");

        var snapshot = _request.Snapshot(_runtime.Registration.Codecs);
        var actorState = _runtime.GetOrCreateActorState(_actor.ActorId);
        var join = new ZLinkDeferredActorJoin(
            _runtime,
            actorState,
            _actor,
            actorState.NativeActorRef?.Generation
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{_actor.ActorId}' does not have a current object generation."),
            _targetSpotId,
            snapshot,
            _timeout ?? _runtime.Registration.DefaultRequestTimeout);
        ZLinkDeferredActorJoinHandlerScope.Register(
            join,
            snapshot.Encode(_runtime.Registration.Codecs).Payload.Bytes.Length);
    }

    private void SetTimeout(TimeSpan timeout)
    {
        if (Volatile.Read(ref _submitted) != 0)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.AlreadySubmitted,
                "Actor Join options cannot change after Defer.");
        ZLinkRequestTimeoutValidation.Validate(timeout, nameof(timeout));
        _timeout = timeout;
    }
}
