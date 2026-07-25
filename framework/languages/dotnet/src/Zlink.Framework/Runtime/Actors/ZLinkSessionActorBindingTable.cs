namespace Zlink.Framework.Runtime.Actors;

internal sealed record ZLinkSessionBindingEntry(
    ZLinkSessionContext Context,
    string BindingToken,
    ZLinkSessionActor ActorRef,
    ulong BindingGeneration,
    ZLinkSessionBindingRoute Route,
    ulong SessionOwnerNodeGeneration,
    ulong AcceptedHighWater,
    string? RelocationHandoffId = null,
    string? CompletedRelocationHandoffId = null,
    int ActiveFrames = 0,
    TaskCompletionSource? DrainSignal = null)
{
    internal ulong ObjectGeneration => Route.Ref.ObjectGeneration;
    internal ulong AuthorityOwnerGeneration => Route.AuthorityOwnerGeneration;
    internal string MeshName => Route.MeshName;
    internal ulong TargetNodeGeneration => Route.TargetNodeGeneration;
    internal ulong OwnerLeaseGeneration => Route.OwnerLeaseGeneration;
}

internal readonly record struct ZLinkSessionBindingRoute
{
    private ZLinkSessionBindingRoute(
        ActorRef actor,
        string meshName,
        ulong targetNodeGeneration,
        ulong authorityOwnerGeneration,
        ulong ownerLeaseGeneration)
    {
        Ref = actor;
        MeshName = meshName;
        TargetNodeGeneration = targetNodeGeneration;
        AuthorityOwnerGeneration = authorityOwnerGeneration;
        OwnerLeaseGeneration = ownerLeaseGeneration;
    }

    internal ActorRef Ref { get; }
    internal string MeshName { get; }
    internal ulong TargetNodeGeneration { get; }
    internal ulong AuthorityOwnerGeneration { get; }
    internal ulong OwnerLeaseGeneration { get; }

    internal static ZLinkSessionBindingRoute Create(
        ActorRef actor,
        string meshName,
        ulong targetNodeGeneration,
        ulong authorityOwnerGeneration,
        ulong ownerLeaseGeneration)
    {
        if (!TryCreate(
                actor,
                meshName,
                targetNodeGeneration,
                authorityOwnerGeneration,
                ownerLeaseGeneration,
                out var route))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorLocationStale,
                $"Actor '{actor.ActorId}' binding requires an exact Mesh, node lifecycle, and owner lease.");
        return route;
    }

    internal static bool TryCreate(
        ActorRef actor,
        string meshName,
        ulong targetNodeGeneration,
        ulong authorityOwnerGeneration,
        ulong ownerLeaseGeneration,
        out ZLinkSessionBindingRoute route)
    {
        if (string.IsNullOrWhiteSpace(actor.ActorId)
            || actor.ObjectGeneration == 0
            || actor.NodeRid.IsEmpty
            || string.IsNullOrWhiteSpace(meshName)
            || !string.Equals(actor.MeshName, meshName, StringComparison.Ordinal)
            || targetNodeGeneration == 0
            || authorityOwnerGeneration == 0
            || ownerLeaseGeneration == 0)
        {
            route = default;
            return false;
        }
        route = new ZLinkSessionBindingRoute(
            actor,
            meshName,
            targetNodeGeneration,
            authorityOwnerGeneration,
            ownerLeaseGeneration);
        return true;
    }

    internal bool MatchesFence(
        string actorId,
        ulong objectGeneration,
        ulong authorityOwnerGeneration,
        string meshName,
        ulong targetNodeGeneration,
        ulong ownerLeaseGeneration) =>
        string.Equals(Ref.ActorId, actorId, StringComparison.Ordinal)
        && Ref.ObjectGeneration == objectGeneration
        && AuthorityOwnerGeneration == authorityOwnerGeneration
        && string.Equals(MeshName, meshName, StringComparison.Ordinal)
        && TargetNodeGeneration == targetNodeGeneration
        && OwnerLeaseGeneration == ownerLeaseGeneration;
}

internal readonly record struct ZLinkSessionRouteSeal(
    string ActorId,
    string BindingToken,
    ulong BindingGeneration,
    ulong ObjectGeneration,
    ulong AuthorityOwnerGeneration,
    string MeshName,
    ulong TargetNodeGeneration,
    ulong OwnerLeaseGeneration,
    ulong SessionOwnerNodeGeneration,
    string HandoffId);

internal readonly record struct ZLinkSessionRouteSealResult(
    bool Acknowledged,
    ulong AcceptedHighWater);

internal readonly record struct ZLinkSessionRouteCommit(
    string ActorId,
    string BindingToken,
    ulong BindingGeneration,
    ulong ObjectGeneration,
    ulong PreviousAuthorityOwnerGeneration,
    ulong TargetAuthorityOwnerGeneration,
    string PreviousMeshName,
    string TargetMeshName,
    ulong PreviousTargetNodeGeneration,
    ulong TargetNodeGeneration,
    ulong PreviousOwnerLeaseGeneration,
    ulong TargetOwnerLeaseGeneration,
    ulong SessionOwnerNodeGeneration,
    ulong AcceptedHighWater,
    string HandoffId,
    ActorRef TargetActor);

internal readonly record struct ZLinkSessionRouteCommitResult(
    bool Acknowledged,
    ulong AcceptedHighWater);

internal readonly record struct ZLinkSessionBindingKey(
    string ActorId,
    string BindingToken);

internal sealed class ZLinkSessionActorBindingTable
{
    private readonly Dictionary<ZLinkSessionBindingKey, ZLinkSessionBindingEntry> _entries = new();

    public ZLinkSessionBindingEntry[] Bind(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken,
        ZLinkSessionActor actorRef,
        ulong bindingGeneration,
        ZLinkSessionBindingRoute route,
        ulong sessionOwnerNodeGeneration)
    {
        if (!string.Equals(actorId, route.Ref.ActorId, StringComparison.Ordinal))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorLocationStale,
                $"Actor '{actorId}' binding route identifies a different Actor.");
        lock (_entries)
        {
            var replaced = _entries
                .Where(entry => string.Equals(entry.Key.ActorId, actorId, StringComparison.Ordinal))
                .Select(entry => entry.Value)
                .ToArray();
            foreach (var entry in replaced)
            {
                _entries.Remove(new ZLinkSessionBindingKey(actorId, entry.BindingToken));
                entry.DrainSignal?.TrySetResult();
            }

            _entries[new ZLinkSessionBindingKey(actorId, bindingToken)] = new ZLinkSessionBindingEntry(
                context,
                bindingToken,
                actorRef,
                bindingGeneration,
                route,
                sessionOwnerNodeGeneration,
                AcceptedHighWater: 0);
            return replaced;
        }
    }

    public bool TryAccept(
        string actorId,
        string bindingToken,
        out ulong acceptedHighWater)
    {
        lock (_entries)
        {
            var key = new ZLinkSessionBindingKey(actorId, bindingToken);
            if (!_entries.TryGetValue(key, out var entry))
            {
                acceptedHighWater = 0;
                return false;
            }
            if (entry.RelocationHandoffId is not null)
            {
                acceptedHighWater = entry.AcceptedHighWater;
                return false;
            }

            acceptedHighWater = checked(entry.AcceptedHighWater + 1);
            _entries[key] = entry with
            {
                AcceptedHighWater = acceptedHighWater,
                ActiveFrames = checked(entry.ActiveFrames + 1)
            };
            return true;
        }
    }

    public void CompleteAccepted(
        string actorId,
        string bindingToken)
    {
        lock (_entries)
        {
            var key = new ZLinkSessionBindingKey(actorId, bindingToken);
            if (!_entries.TryGetValue(key, out var entry)
                || entry.ActiveFrames == 0)
                return;
            var remaining = entry.ActiveFrames - 1;
            _entries[key] = entry with { ActiveFrames = remaining };
            if (remaining == 0)
                entry.DrainSignal?.TrySetResult();
        }
    }

    public async ValueTask<ZLinkSessionRouteSealResult> SealRouteAsync(
        ZLinkSessionRouteSeal request,
        CancellationToken cancellationToken)
    {
        Task? drain = null;
        ulong acceptedHighWater;
        lock (_entries)
        {
            var key = new ZLinkSessionBindingKey(
                request.ActorId,
                request.BindingToken);
            if (!_entries.TryGetValue(key, out var entry)
                || entry.BindingGeneration != request.BindingGeneration
                || !entry.Route.MatchesFence(
                    request.ActorId,
                    request.ObjectGeneration,
                    request.AuthorityOwnerGeneration,
                    request.MeshName,
                    request.TargetNodeGeneration,
                    request.OwnerLeaseGeneration)
                || entry.SessionOwnerNodeGeneration
                != request.SessionOwnerNodeGeneration)
                return new ZLinkSessionRouteSealResult(
                    false,
                    entry?.AcceptedHighWater ?? 0);

            if (entry.RelocationHandoffId is { } current
                && !string.Equals(current, request.HandoffId, StringComparison.Ordinal))
                return new ZLinkSessionRouteSealResult(
                    false,
                    entry.AcceptedHighWater);

            var signal = entry.ActiveFrames == 0
                ? null
                : entry.DrainSignal
                  ?? new TaskCompletionSource(
                      TaskCreationOptions.RunContinuationsAsynchronously);
            _entries[key] = entry with
            {
                RelocationHandoffId = request.HandoffId,
                DrainSignal = signal
            };
            drain = signal?.Task;
            acceptedHighWater = entry.AcceptedHighWater;
        }
        if (drain is not null)
            await drain.WaitAsync(cancellationToken).ConfigureAwait(false);
        lock (_entries)
        {
            var key = new ZLinkSessionBindingKey(
                request.ActorId,
                request.BindingToken);
            return _entries.TryGetValue(key, out var current)
                   && string.Equals(
                       current.RelocationHandoffId,
                       request.HandoffId,
                       StringComparison.Ordinal)
                   && current.ActiveFrames == 0
                ? new ZLinkSessionRouteSealResult(
                    true,
                    current.AcceptedHighWater)
                : new ZLinkSessionRouteSealResult(false, acceptedHighWater);
        }
    }

    public bool AbortRouteSeal(ZLinkSessionRouteSeal request)
    {
        lock (_entries)
        {
            var key = new ZLinkSessionBindingKey(
                request.ActorId,
                request.BindingToken);
            if (!_entries.TryGetValue(key, out var entry)
                || entry.BindingGeneration != request.BindingGeneration
                || !entry.Route.MatchesFence(
                    request.ActorId,
                    request.ObjectGeneration,
                    request.AuthorityOwnerGeneration,
                    request.MeshName,
                    request.TargetNodeGeneration,
                    request.OwnerLeaseGeneration)
                || entry.SessionOwnerNodeGeneration
                != request.SessionOwnerNodeGeneration
                || !string.Equals(
                    entry.RelocationHandoffId,
                    request.HandoffId,
                    StringComparison.Ordinal))
                return false;
            _entries[key] = entry with
            {
                RelocationHandoffId = null,
                DrainSignal = null
            };
            return true;
        }
    }

    public bool UnsealCommittedRoute(
        ZLinkSessionRouteCommit request)
    {
        if (!ZLinkSessionBindingRoute.TryCreate(
                request.TargetActor,
                request.TargetMeshName,
                request.TargetNodeGeneration,
                request.TargetAuthorityOwnerGeneration,
                request.TargetOwnerLeaseGeneration,
                out var targetRoute))
            return false;
        lock (_entries)
        {
            var key = new ZLinkSessionBindingKey(
                request.ActorId,
                request.BindingToken);
            if (!_entries.TryGetValue(key, out var entry)
                || entry.BindingGeneration != request.BindingGeneration
                || entry.Route != targetRoute
                || entry.SessionOwnerNodeGeneration
                != request.SessionOwnerNodeGeneration
                || entry.AcceptedHighWater != request.AcceptedHighWater
                || !string.Equals(
                    entry.RelocationHandoffId,
                    request.HandoffId,
                    StringComparison.Ordinal))
                return false;
            _entries[key] = entry with
            {
                RelocationHandoffId = null,
                DrainSignal = null
            };
            return true;
        }
    }

    public ZLinkSessionRouteCommitResult CommitRoute(
        ZLinkSessionRouteCommit request)
    {
        lock (_entries)
        {
            var key = new ZLinkSessionBindingKey(
                request.ActorId,
                request.BindingToken);
            if (!_entries.TryGetValue(key, out var entry)
                || entry.BindingGeneration != request.BindingGeneration
                || entry.ObjectGeneration != request.ObjectGeneration
                || entry.SessionOwnerNodeGeneration
                != request.SessionOwnerNodeGeneration
                || entry.AcceptedHighWater != request.AcceptedHighWater)
                return new ZLinkSessionRouteCommitResult(
                    false,
                    entry?.AcceptedHighWater ?? 0);
            if (!ZLinkSessionBindingRoute.TryCreate(
                    request.TargetActor,
                    request.TargetMeshName,
                    request.TargetNodeGeneration,
                    request.TargetAuthorityOwnerGeneration,
                    request.TargetOwnerLeaseGeneration,
                    out var targetRoute)
                || targetRoute.Ref.ObjectGeneration != request.ObjectGeneration
                || request.TargetAuthorityOwnerGeneration
                <= request.PreviousAuthorityOwnerGeneration)
                return new ZLinkSessionRouteCommitResult(
                    false,
                    entry.AcceptedHighWater);

            if (entry.Route == targetRoute)
                return new ZLinkSessionRouteCommitResult(
                    string.Equals(
                        entry.CompletedRelocationHandoffId,
                        request.HandoffId,
                        StringComparison.Ordinal),
                    entry.AcceptedHighWater);

            if (!entry.Route.MatchesFence(
                    request.ActorId,
                    request.ObjectGeneration,
                    request.PreviousAuthorityOwnerGeneration,
                    request.PreviousMeshName,
                    request.PreviousTargetNodeGeneration,
                    request.PreviousOwnerLeaseGeneration)
                || !string.Equals(
                    entry.RelocationHandoffId,
                    request.HandoffId,
                    StringComparison.Ordinal))
                return new ZLinkSessionRouteCommitResult(
                    false,
                    entry.AcceptedHighWater);

            _entries[key] = entry with
            {
                Route = targetRoute,
                CompletedRelocationHandoffId = request.HandoffId
            };
            return new ZLinkSessionRouteCommitResult(
                true,
                entry.AcceptedHighWater);
        }
    }

    public bool TryGet(
        string actorId,
        string bindingToken,
        out ZLinkSessionBindingEntry entry)
    {
        lock (_entries)
        {
            return _entries.TryGetValue(
                new ZLinkSessionBindingKey(actorId, bindingToken),
                out entry!);
        }
    }

    public bool TryGetRoute(
        string actorId,
        string bindingToken,
        ZLinkSessionActor actorRef,
        out ZLinkSessionBindingRoute route)
    {
        lock (_entries)
        {
            if (_entries.TryGetValue(
                    new ZLinkSessionBindingKey(actorId, bindingToken),
                    out var entry)
                && ReferenceEquals(entry.ActorRef, actorRef))
            {
                route = entry.Route;
                return true;
            }
            route = default;
            return false;
        }
    }

    public void Unbind(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken)
    {
        lock (_entries)
        {
            var key = new ZLinkSessionBindingKey(actorId, bindingToken);
            if (_entries.TryGetValue(key, out var existing)
                && ReferenceEquals(existing.Context, context)
                && string.Equals(existing.BindingToken, bindingToken, StringComparison.Ordinal))
            {
                _entries.Remove(key);
                existing.DrainSignal?.TrySetResult();
            }
        }
    }

    public bool TryGet(
        string actorId,
        string bindingToken,
        out ZLinkSessionContext context)
    {
        lock (_entries)
        {
            var key = new ZLinkSessionBindingKey(actorId, bindingToken);
            if (_entries.TryGetValue(key, out var entry))
            {
                context = entry.Context;
                return true;
            }

            context = null!;
            return false;
        }
    }

    public bool TryGetByActorId(
        string actorId,
        out ZLinkSessionContext context)
    {
        lock (_entries)
        {
            foreach (var entry in _entries)
                if (string.Equals(entry.Key.ActorId, actorId, StringComparison.Ordinal))
                {
                    context = entry.Value.Context;
                    return true;
                }

            context = null!;
            return false;
        }
    }

    public bool TryGetEntryByActorId(
        string actorId,
        out ZLinkSessionBindingEntry entry)
    {
        lock (_entries)
        {
            foreach (var candidate in _entries)
                if (string.Equals(
                        candidate.Key.ActorId,
                        actorId,
                        StringComparison.Ordinal))
                {
                    entry = candidate.Value;
                    return true;
                }
            entry = null!;
            return false;
        }
    }

    public IReadOnlyCollection<IZLinkSessionActor> SnapshotActors(
        ZLinkSessionContext context)
    {
        lock (_entries)
        {
            return _entries.Values
                .Where(entry => ReferenceEquals(entry.Context, context))
                .Select(static entry => (IZLinkSessionActor)entry.ActorRef)
                .ToArray();
        }
    }

    public ZLinkSessionActor? FindActor(
        ZLinkSessionContext context,
        string actorId)
    {
        lock (_entries)
        {
            return _entries.Values
                .Where(entry => ReferenceEquals(entry.Context, context))
                .FirstOrDefault(entry => string.Equals(
                    entry.ActorRef.ActorId,
                    actorId,
                    StringComparison.Ordinal))
                ?.ActorRef;
        }
    }

    public void ResetGeneration()
    {
        lock (_entries)
        {
            foreach (var entry in _entries.Values)
                entry.DrainSignal?.TrySetResult();
            _entries.Clear();
        }
    }
}
