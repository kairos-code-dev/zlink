namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorManagerService(ZLinkFrameworkRuntime runtime) : IZLinkActorManager
{
    private long _nextPlacementSelection;

    public IZLinkActorCreateCall Create(string actorId, string actorType) =>
        new ZLinkActorCreateCall(
            runtime.Registration.DefaultRequestTimeout,
            (meshName, request, timeout, cancellationToken) =>
                SubmitAsync(actorId, actorType, true, meshName, request, timeout, cancellationToken));

    public IZLinkActorGetOrCreateCall GetOrCreate(string actorId, string actorType) =>
        new ZLinkActorGetOrCreateCall(
            runtime.Registration.DefaultRequestTimeout,
            (meshName, request, timeout, cancellationToken) =>
                SubmitAsync(actorId, actorType, false, meshName, request, timeout, cancellationToken));

    public async ValueTask<ActorRef?> FindAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        using var operation = runtime.EnterOperation();
        cancellationToken.ThrowIfCancellationRequested();
        var store = runtime.Registration.Locations.ResolveStore();
        if (store is null)
            return runtime.TryGetCreatedActorState(actorId, out var local)
                   && local.NativeActorRef is { } actorRef
                ? actorRef.ToNative()
                : null;
        var read = await store.ReadAuthorityAsync(
                ZLinkActorAuthorityPayloadCodec.AuthorityKey(actorId),
                cancellationToken)
            .ConfigureAwait(false);
        if (read is not ZLinkAuthorityReadResult.Found found
            || found.Snapshot.Allocation.State != ZLinkPlacementAllocationState.Active
            || !ZLinkActorAuthorityPayloadCodec.TryDecode(
                found.Snapshot.Payload.Span,
                out var authority)
            || authority.State != ZLinkActorAuthorityState.Ready)
            return null;
        return new ActorRef(
            authority.NodeRid,
            actorId,
            found.Snapshot.ObjectGeneration);
    }

    public async ValueTask<SpotRef?> FindSpotAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        using var operation = runtime.EnterOperation();
        cancellationToken.ThrowIfCancellationRequested();
        var store = runtime.Registration.Locations.ResolveStore();
        if (store is null)
        {
            if (!runtime.TryGetCreatedActorState(actorId, out var state)
                || state.SpotId is not { } spotId)
                return null;
            return await runtime.FindAsync(spotId, cancellationToken).ConfigureAwait(false);
        }
        var read = await store.ReadAuthorityAsync(
                ZLinkActorAuthorityPayloadCodec.AuthorityKey(actorId),
                cancellationToken)
            .ConfigureAwait(false);
        if (read is not ZLinkAuthorityReadResult.Found found
            || found.Snapshot.Allocation.State != ZLinkPlacementAllocationState.Active
            || !ZLinkActorAuthorityPayloadCodec.TryDecode(
                found.Snapshot.Payload.Span,
                out var authority)
            || authority.State != ZLinkActorAuthorityState.Ready)
            return null;
        return new SpotRef(
            authority.CurrentSpotId,
            authority.CurrentSpotGeneration,
            authority.MeshName,
            authority.NodeRid);
    }

    public async ValueTask<bool> DestroyAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default)
    {
        using var operation = runtime.EnterOperation();
        cancellationToken.ThrowIfCancellationRequested();
        if (!runtime.TryGetCreatedActorState(actor.ActorId, out var state)
            || state.NativeActorRef is not { } current
            || state.Actor is not { } instance)
        {
            var resolved = await FindAsync(actor.ActorId, cancellationToken)
                .ConfigureAwait(false);
            if (resolved is null)
                return false;
            if (resolved.Value != actor)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorGenerationStale,
                    $"Actor '{actor.ActorId}' generation is stale.");
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestTargetNotFound,
                $"Remote Actor destroy is not connected for owner node '{actor.NodeRid}'.");
        }
        if (current.Generation != actor.Generation || current.NodeRid != actor.NodeRid)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorGenerationStale,
                $"Actor '{actor.ActorId}' generation is stale.");

        await runtime.DestroyActorAsync(
                state.LiveActivation?.NodeRid ?? actor.NodeRid,
                instance,
                cancellationToken)
            .ConfigureAwait(false);
        return true;
    }

    private async ValueTask<ZLinkActorCreateResult> SubmitAsync(
        string actorId,
        string actorType,
        bool createOnly,
        string? meshName,
        ZLinkMessage createRequest,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var deadlineAt = DateTimeOffset.UtcNow.Add(timeout);
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        deadline.CancelAfter(timeout);
        using var operation = runtime.EnterOperation();
        var source = runtime.ResolveActorCreationSource(meshName);
        var store = runtime.Registration.Locations.ResolveStore()
                    ?? throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.InvalidConfiguration,
                        "Actor creation requires a Location Store.");
        var selectedMesh = source.Registration.SpotMeshChannelName
                           ?? source.Registration.SpotNodeName;
        var descriptors = await store.ListMeshNodesAsync(selectedMesh, deadline.Token)
            .ConfigureAwait(false);
        var eligible = descriptors
            .Where(candidate => IsEligibleCandidate(candidate, actorType))
            .OrderBy(static candidate => candidate.Rid.ToHex(), StringComparer.Ordinal)
            .ToList();
        var encoded = createRequest.Encode(runtime.Registration.Codecs);
        var applicationPayload = ZLinkApplicationPayloadEnvelopeCodec.Encode(
            string.Empty,
            encoded.ContentType,
            encoded.Payload.Bytes.Span);
        var applicationHash = System.Security.Cryptography.SHA256.HashData(applicationPayload);
        var contentReference = ZLinkInlineCreationIntentCodec.Encode(applicationPayload);
        var key = ZLinkActorAuthorityPayloadCodec.AuthorityKey(actorId);
        while (true)
        {
            var target = ZLinkWeightedSelector.Select(
                    eligible,
                    static candidate => candidate.PlacementWeight,
                    ref _nextPlacementSelection)
                ?? throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.PlacementCapacityExhausted,
                    $"No Ready Actor target is available for '{actorType}'.",
                    true);
            var entry = await store.ResolveSpotAsync(
                    new ZLinkSpotLocationKey(target.EntrySpotId!),
                    deadline.Token)
                .ConfigureAwait(false);
            if (entry is null
                || entry.SpotKind != ZLinkSpotKind.Entry
                || entry.OwnerNodeRid != target.Rid
                || entry.OwnerNodeGeneration != target.LifecycleGeneration)
            {
                eligible.Remove(target);
                continue;
            }
            var owner = new ZLinkLocationOwnerToken(target.OwnerId, target.LeaseGeneration);
            var creating = ZLinkActorAuthorityPayloadCodec.Encode(
                new ZLinkActorAuthorityPayload(
                    ZLinkActorAuthorityState.Creating,
                    actorType,
                    actorId,
                    entry.SpotId,
                    entry.SpotGeneration,
                    ZLinkSpotKind.Entry,
                    owner.OwnerId,
                    checked((ulong)owner.LeaseGeneration),
                    selectedMesh,
                    target.Rid,
                    target.LifecycleGeneration));
            var reserve = await store.ReserveAsync(
                    new ZLinkObjectReservationRequest(
                        ZLinkPlacementObjectKind.Actor,
                        key,
                        actorType,
                        contentReference,
                        applicationHash,
                        applicationPayload.Length,
                        new ZLinkMeshNodeDescriptorKey(selectedMesh, target.Rid),
                        target.LifecycleGeneration,
                        owner,
                        creating,
                        1),
                    deadline.Token)
                .ConfigureAwait(false);
            if (reserve is ZLinkObjectReserveResult.PlacementCapacityExhausted)
            {
                eligible.Remove(target);
                continue;
            }
            if (reserve is ZLinkObjectReserveResult.TypeMismatch)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorTypeMismatch,
                    $"Actor '{actorId}' is not registered as '{actorType}'.");
            if (reserve is ZLinkObjectReserveResult.AlreadyExists existing)
            {
                if (createOnly)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorAlreadyExists,
                        $"Actor '{actorId}' already exists.");
                var joined = await JoinExistingAsync(
                        store,
                        key,
                        actorId,
                        actorType,
                        existing.Current,
                        deadline.Token)
                    .ConfigureAwait(false);
                if (joined is not null)
                    return joined;
                continue;
            }
            if (reserve is not ZLinkObjectReserveResult.Reserved reserved)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorLocationStale,
                    $"Actor '{actorId}' creation reservation changed.",
                    true);

            var reservation = reserved.Reservation;
            try
            {
                var fence = new ObjectReservationFence(
                    reservation.ReservationVersion,
                    reservation.StoreVersion,
                    reservation.ObjectGeneration,
                    reservation.AuthorityOwnerGeneration,
                    target.Rid,
                    target.LifecycleGeneration,
                    owner.OwnerId,
                    checked((ulong)owner.LeaseGeneration),
                    1);
                var deadlineUnixMs = checked((ulong)deadlineAt.ToUnixTimeMilliseconds());
                if (target.Rid == source.Node.RoutingId)
                {
                    var local = await source.CreateActorLocalAsync(
                            actorId,
                            actorType,
                            fence,
                            deadlineUnixMs,
                            deadline.Token)
                        .ConfigureAwait(false);
                    return DecodeRemoteResult(local.Completion, local.Reply);
                }

                var remaining = deadlineAt - DateTimeOffset.UtcNow;
                if (remaining <= TimeSpan.Zero)
                    throw new TimeoutException("The Actor create deadline elapsed.");
                var remote = await source.Node.CreateActorRemoteAsync(
                        target.Rid,
                        actorId,
                        actorType,
                        fence,
                        deadlineUnixMs,
                        remaining,
                        deadline.Token)
                    .ConfigureAwait(false);
                return DecodeRemoteResult(remote.Completion, remote.Reply);
            }
            catch (ZlinkSubmitException)
            {
                await store.AbortAsync(reservation, CancellationToken.None)
                    .ConfigureAwait(false);
                throw;
            }
        }
    }

    internal static bool IsEligibleCandidate(
        ZLinkMeshNodeDescriptor candidate,
        string actorType) =>
        candidate.State == ZLinkFrameworkRuntimeState.Serving
        && candidate.ObjectRole == ZLinkMeshNodeObjectRole.Server
        && candidate.PlacementWeight > 0
        && candidate.EntrySpotId is not null
        && (candidate.Capacity.Actors.Limit == 0
            || candidate.Capacity.Actors.Active
            + (long)candidate.Capacity.Actors.Reserved
            < candidate.Capacity.Actors.Limit)
        && candidate.ObjectCapabilities.Any(capability =>
            capability.ObjectKind == ZLinkPlacementObjectKind.Actor
            && string.Equals(capability.StableType, actorType, StringComparison.Ordinal));

    private async ValueTask<ZLinkActorCreateResult?> JoinExistingAsync(
        IZLinkLocationStore store,
        ZLinkAuthorityKey key,
        string actorId,
        string actorType,
        ZLinkAuthoritySnapshot current,
        CancellationToken cancellationToken)
    {
        while (true)
        {
            if (current.Allocation.ObjectKind != ZLinkPlacementObjectKind.Actor
                || !string.Equals(current.Allocation.StableType, actorType, StringComparison.Ordinal)
                || !ZLinkActorAuthorityPayloadCodec.TryDecode(current.Payload.Span, out var authority))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorTypeMismatch,
                    $"Actor '{actorId}' does not use type '{actorType}'.");
            if (current.Allocation.State == ZLinkPlacementAllocationState.Active
                && authority.State == ZLinkActorAuthorityState.Ready)
                return new ZLinkActorCreateResult.Existing(
                    new ActorRef(authority.NodeRid, actorId, current.ObjectGeneration));
            if (authority.State != ZLinkActorAuthorityState.Creating)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorLocationStale,
                    $"Actor '{actorId}' creation state is invalid.",
                    true);

            await Task.Delay(TimeSpan.FromMilliseconds(10), cancellationToken)
                .ConfigureAwait(false);
            var read = await store.ReadAuthorityAsync(key, cancellationToken)
                .ConfigureAwait(false);
            if (read is ZLinkAuthorityReadResult.Found next)
            {
                current = next.Snapshot;
                continue;
            }
            return null;
        }
    }

    private ZLinkActorCreateResult DecodeRemoteResult(
        ActorCreateCompletion completion,
        IReadOnlyList<Message> replyParts)
    {
        ZLinkMessage? reply = null;
        try
        {
            if (replyParts.Count > 1)
            {
                var header = ZLinkEnvelopeCodec.DecodeHeader(replyParts);
                reply = ZLinkMessage.FromEnvelopePayload(
                    header.ContentType,
                    replyParts[1],
                    runtime.Registration.Codecs);
            }
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(replyParts);
        }
        return completion.Result switch
        {
            ActorCreateResult.Existing =>
                new ZLinkActorCreateResult.Existing(completion.Actor),
            ActorCreateResult.Created =>
                new ZLinkActorCreateResult.Created(completion.Actor, reply),
            _ => new ZLinkActorCreateResult.Rejected(reply)
        };
    }

}

internal abstract class ZLinkActorCreateCallBase
{
    private readonly Func<string?, ZLinkMessage, TimeSpan, CancellationToken,
        ValueTask<ZLinkActorCreateResult>> _submit;
    private string? _meshName;
    private ZLinkMessage _request = ZLinkMessage.Empty;
    private TimeSpan? _timeout;
    private bool _requestSet;
    private int _submitted;

    protected ZLinkActorCreateCallBase(
        Func<string?, ZLinkMessage, TimeSpan, CancellationToken,
            ValueTask<ZLinkActorCreateResult>> submit)
    {
        _submit = submit;
    }

    protected void SetMesh(string meshName)
    {
        if (_meshName is not null) Duplicate("InMesh");
        if (string.IsNullOrWhiteSpace(meshName))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.InvalidConfiguration,
                "MeshName is required.");
        _meshName = meshName;
    }

    protected void SetRequest(ZLinkMessage request)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (_requestSet) Duplicate("Request");
        _request = request;
        _requestSet = true;
    }

    protected void SetTimeout(TimeSpan timeout)
    {
        if (_timeout is not null) Duplicate("Timeout");
        if (timeout <= TimeSpan.Zero)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.InvalidConfiguration,
                "Timeout must be positive.");
        _timeout = timeout;
    }

    protected ValueTask<ZLinkActorCreateResult> SubmitAsync(
        TimeSpan defaultTimeout,
        CancellationToken cancellationToken)
    {
        if (Interlocked.Exchange(ref _submitted, 1) != 0)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.AlreadySubmitted,
                "The Actor create call was already submitted.");
        return _submit(_meshName, _request, _timeout ?? defaultTimeout, cancellationToken);
    }

    private static void Duplicate(string option) =>
        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.InvalidConfiguration,
            $"{option} was already configured.");
}

internal sealed class ZLinkActorCreateCall(
    TimeSpan defaultTimeout,
    Func<string?, ZLinkMessage, TimeSpan, CancellationToken,
        ValueTask<ZLinkActorCreateResult>> submit)
    : ZLinkActorCreateCallBase(submit), IZLinkActorCreateCall
{
    public IZLinkActorCreateCall InMesh(string meshName) { SetMesh(meshName); return this; }
    public IZLinkActorCreateCall Request(ZLinkMessage request) { SetRequest(request); return this; }
    public IZLinkActorCreateCall Request<TRequest>(TRequest request)
    {
        SetRequest(ZLinkMessage.From(request));
        return this;
    }
    public IZLinkActorCreateCall Timeout(TimeSpan timeout) { SetTimeout(timeout); return this; }
    public ValueTask<ZLinkActorCreateResult> Async(CancellationToken cancellationToken = default) =>
        SubmitAsync(defaultTimeout, cancellationToken);
}

internal sealed class ZLinkActorGetOrCreateCall(
    TimeSpan defaultTimeout,
    Func<string?, ZLinkMessage, TimeSpan, CancellationToken,
        ValueTask<ZLinkActorCreateResult>> submit)
    : ZLinkActorCreateCallBase(submit), IZLinkActorGetOrCreateCall
{
    public IZLinkActorGetOrCreateCall InMesh(string meshName) { SetMesh(meshName); return this; }
    public IZLinkActorGetOrCreateCall Request(ZLinkMessage request) { SetRequest(request); return this; }
    public IZLinkActorGetOrCreateCall Request<TRequest>(TRequest request)
    {
        SetRequest(ZLinkMessage.From(request));
        return this;
    }
    public IZLinkActorGetOrCreateCall Timeout(TimeSpan timeout) { SetTimeout(timeout); return this; }
    public ValueTask<ZLinkActorCreateResult> Async(CancellationToken cancellationToken = default) =>
        SubmitAsync(defaultTimeout, cancellationToken);
}
