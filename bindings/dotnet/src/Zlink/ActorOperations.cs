// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Native;

namespace Systems.Zlink;

// --- Public results ---

public sealed record ActorJoinResult(RequestResult Result, ActorRef Actor,
    RoutingId JoinedSpotRid, ulong JoinEpoch, uint Flags);

public sealed record ActorLookupResult(RequestResult Result, ActorRef Actor,
    uint Flags);

// --- Public delegates ---

public delegate void ActorJoinHandler(ActorJoinResult result,
    IReadOnlyList<Message> replyParts);

public delegate void ActorLookupHandler(ActorLookupResult result);

public delegate void ActorLifecycleHandler(Spot spot,
    SpotActorLifecycleInfo info);

public delegate void ReplyHandler(RequestResult result,
    IReadOnlyList<Message> parts);

// --- Public operation builder interfaces ---

public interface ActorJoinOperation
{
    ActorJoinSubmitOperation Message(Message message);
}

public interface ActorJoinSubmitOperation
{
    ActorJoinSubmitOperation Message(Message message);
    ActorJoinSubmitOperation Timeout(TimeSpan timeout);
    ActorJoinCallbackSubmitOperation Flags(SendFlags flags);
    Task<(ActorJoinResult Result, IReadOnlyList<Message> Parts)> SubmitAsync(
        CancellationToken ct = default);
    bool Submit(ActorJoinHandler callback);
}

public interface ActorJoinCallbackSubmitOperation
{
    ActorJoinCallbackSubmitOperation Message(Message message);
    ActorJoinCallbackSubmitOperation Timeout(TimeSpan timeout);
    ActorJoinCallbackSubmitOperation Flags(SendFlags flags);
    bool Submit(ActorJoinHandler callback);
}

public interface ActorJoinReplyOperation
{
    ActorJoinReplyOperation Message(Message message);
    void Submit();
}

public interface ActorLeaveOperation
{
    ActorLeaveOperation Timeout(TimeSpan timeout);
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    bool Submit(ReplyHandler callback);
}

public interface ActorDestroyOperation
{
    ActorDestroyOperation Timeout(TimeSpan timeout);
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    bool Submit(ReplyHandler callback);
}

public interface ActorLookupOperation
{
    ActorLookupOperation Timeout(TimeSpan timeout);
    Task<ActorLookupResult> SubmitAsync(CancellationToken ct = default);
    bool Submit(ActorLookupHandler callback);
}

public interface ActorBindOperation
{
    ActorBindOperation Timeout(TimeSpan timeout);
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    bool Submit(ReplyHandler callback);
}

public interface ActorUnbindOperation
{
    ActorUnbindOperation Timeout(TimeSpan timeout);
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    bool Submit(ReplyHandler callback);
}

// --- Internal implementations ---

internal enum ActorJoinSource
{
    Actor,
    Node
}

internal sealed class ActorJoinOperationImpl : ActorJoinOperation,
    ActorJoinSubmitOperation, ActorJoinCallbackSubmitOperation
{
    private readonly SpotNode _node;
    private readonly ActorRef _actor;
    private readonly RoutingId _destNodeRid;
    private readonly RoutingId _destSpotRid;
    private readonly List<Message> _parts = new();
    private TimeSpan _timeout;
    private SendFlags _flags;
    private bool _callbackStage;
    private bool _submitted;

    internal ActorJoinOperationImpl(SpotNode node, ActorRef actor,
        RoutingId destNodeRid, RoutingId destSpotRid)
    {
        _node = node;
        _actor = actor;
        _destNodeRid = destNodeRid;
        _destSpotRid = destSpotRid;
    }

    public ActorJoinSubmitOperation Message(Message message)
    {
        AddMessage(message);
        return this;
    }

    ActorJoinSubmitOperation ActorJoinSubmitOperation.Message(Message message)
    {
        AddMessage(message);
        return this;
    }

    ActorJoinCallbackSubmitOperation ActorJoinCallbackSubmitOperation.Message(
        Message message)
    {
        AddMessage(message);
        return this;
    }

    public ActorJoinSubmitOperation Timeout(TimeSpan timeout)
    {
        EnsureNotSubmitted();
        _timeout = timeout;
        return this;
    }

    ActorJoinCallbackSubmitOperation ActorJoinCallbackSubmitOperation.Timeout(
        TimeSpan timeout)
    {
        EnsureNotSubmitted();
        _timeout = timeout;
        return this;
    }

    public ActorJoinCallbackSubmitOperation Flags(SendFlags flags)
    {
        EnsureNotSubmitted();
        _callbackStage = true;
        _flags = flags;
        return this;
    }

    ActorJoinCallbackSubmitOperation ActorJoinCallbackSubmitOperation.Flags(
        SendFlags flags)
    {
        EnsureNotSubmitted();
        _flags = flags;
        return this;
    }

    public Task<(ActorJoinResult Result, IReadOnlyList<Message> Parts)>
        SubmitAsync(CancellationToken ct = default)
    {
        EnsureReady();
        if (_callbackStage)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
        _submitted = true;
        return ActorInterop.JoinActorAsync(_node, _actor, _destNodeRid,
            _destSpotRid, _parts, _timeout, _flags, ct);
    }

    public bool Submit(ActorJoinHandler callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        EnsureReady();
        _submitted = true;
        return ActorInterop.JoinActorCallback(_node, _actor, _destNodeRid,
            _destSpotRid, _parts, _timeout, _flags, callback);
    }

    private void AddMessage(Message message)
    {
        EnsureNotSubmitted();
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        _parts.Add(message);
    }

    private void EnsureReady()
    {
        EnsureNotSubmitted();
        if (_parts.Count == 0)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidArgument);
    }

    private void EnsureNotSubmitted()
    {
        if (_submitted)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
    }
}

internal sealed class ActorJoinReplyOperationImpl : ActorJoinReplyOperation
{
    private readonly Spot _spot;
    private readonly ActorJoinRequest _request;
    private readonly bool _accepted;
    private readonly List<Message> _parts = new();
    private bool _submitted;

    internal ActorJoinReplyOperationImpl(Spot spot, ActorJoinRequest request,
        bool accepted)
    {
        _spot = spot;
        _request = request;
        _accepted = accepted;
    }

    public ActorJoinReplyOperation Message(Message message)
    {
        if (_submitted)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        _parts.Add(message);
        return this;
    }

    public void Submit()
    {
        if (_submitted)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
        _submitted = true;
        _spot.ReplyActorJoinInternal(_request.Info, _accepted, _parts);
    }
}

internal sealed class ActorLeaveOperationImpl : ActorLeaveOperation
{
    private readonly SpotNode _node;
    private readonly ActorRef _actor;
    private readonly RoutingId _currentSpotRid;
    private TimeSpan _timeout;
    private bool _submitted;

    internal ActorLeaveOperationImpl(SpotNode node, ActorRef actor,
        RoutingId currentSpotRid)
    {
        _node = node;
        _actor = actor;
        _currentSpotRid = currentSpotRid;
    }

    public ActorLeaveOperation Timeout(TimeSpan timeout)
    {
        EnsureNotSubmitted();
        _timeout = timeout;
        return this;
    }

    public Task<IReadOnlyList<Message>> SubmitAsync(
        CancellationToken ct = default)
    {
        EnsureNotSubmitted();
        _submitted = true;
        return ActorInterop.LeaveActorAsync(_node, _actor, _currentSpotRid,
            _timeout, ct);
    }

    public bool Submit(ReplyHandler callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        EnsureNotSubmitted();
        _submitted = true;
        return ActorInterop.LeaveActorCallback(_node, _actor, _currentSpotRid,
            _timeout, callback);
    }

    private void EnsureNotSubmitted()
    {
        if (_submitted)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
    }
}

internal sealed class ActorDestroyOperationImpl : ActorDestroyOperation
{
    private readonly SpotNode _node;
    private readonly ActorRef _actor;
    private TimeSpan _timeout;
    private bool _submitted;

    internal ActorDestroyOperationImpl(SpotNode node, ActorRef actor)
    {
        _node = node;
        _actor = actor;
    }

    public ActorDestroyOperation Timeout(TimeSpan timeout)
    {
        EnsureNotSubmitted();
        _timeout = timeout;
        return this;
    }

    public Task<IReadOnlyList<Message>> SubmitAsync(
        CancellationToken ct = default)
    {
        EnsureNotSubmitted();
        _submitted = true;
        return ActorInterop.DestroyActorAsync(_node, _actor, _timeout, ct);
    }

    public bool Submit(ReplyHandler callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        EnsureNotSubmitted();
        _submitted = true;
        return ActorInterop.DestroyActorCallback(_node, _actor, _timeout,
            callback);
    }

    private void EnsureNotSubmitted()
    {
        if (_submitted)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
    }
}

internal sealed class ActorLookupOperationImpl : ActorLookupOperation
{
    private readonly SpotNode _node;
    private readonly RoutingId _targetNodeRid;
    private readonly string _actorId;
    private TimeSpan _timeout;
    private bool _submitted;

    internal ActorLookupOperationImpl(SpotNode node, RoutingId targetNodeRid,
        string actorId)
    {
        _node = node;
        _targetNodeRid = targetNodeRid;
        _actorId = actorId;
    }

    public ActorLookupOperation Timeout(TimeSpan timeout)
    {
        EnsureNotSubmitted();
        _timeout = timeout;
        return this;
    }

    public Task<ActorLookupResult> SubmitAsync(CancellationToken ct = default)
    {
        EnsureNotSubmitted();
        _submitted = true;
        return ActorInterop.RemoteActorGetRefAsync(_node, _targetNodeRid,
            _actorId, _timeout, ct);
    }

    public bool Submit(ActorLookupHandler callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        EnsureNotSubmitted();
        _submitted = true;
        return ActorInterop.RemoteActorGetRefCallback(_node, _targetNodeRid,
            _actorId, _timeout, callback);
    }

    private void EnsureNotSubmitted()
    {
        if (_submitted)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
    }
}

internal sealed class ActorBindOperationImpl : ActorBindOperation
{
    private readonly StreamSocket _stream;
    private readonly RoutingId _sessionRid;
    private readonly ActorRef _actor;
    private TimeSpan _timeout;
    private bool _submitted;

    internal ActorBindOperationImpl(StreamSocket stream, RoutingId sessionRid,
        ActorRef actor)
    {
        _stream = stream;
        _sessionRid = sessionRid;
        _actor = actor;
    }

    public ActorBindOperation Timeout(TimeSpan timeout)
    {
        EnsureNotSubmitted();
        _timeout = timeout;
        return this;
    }

    public Task<IReadOnlyList<Message>> SubmitAsync(
        CancellationToken ct = default)
    {
        EnsureNotSubmitted();
        _submitted = true;
        return ActorInterop.BindActorAsync(_stream, _sessionRid, _actor,
            _timeout, ct);
    }

    public bool Submit(ReplyHandler callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        EnsureNotSubmitted();
        _submitted = true;
        return ActorInterop.BindActorCallback(_stream, _sessionRid, _actor,
            _timeout, callback);
    }

    private void EnsureNotSubmitted()
    {
        if (_submitted)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
    }
}

internal sealed class ActorUnbindOperationImpl : ActorUnbindOperation
{
    private readonly StreamSocket _stream;
    private readonly RoutingId _sessionRid;
    private readonly string _actorId;
    private TimeSpan _timeout;
    private bool _submitted;

    internal ActorUnbindOperationImpl(StreamSocket stream, RoutingId sessionRid,
        string actorId)
    {
        _stream = stream;
        _sessionRid = sessionRid;
        _actorId = actorId;
    }

    public ActorUnbindOperation Timeout(TimeSpan timeout)
    {
        EnsureNotSubmitted();
        _timeout = timeout;
        return this;
    }

    public Task<IReadOnlyList<Message>> SubmitAsync(
        CancellationToken ct = default)
    {
        EnsureNotSubmitted();
        _submitted = true;
        return ActorInterop.UnbindActorAsync(_stream, _sessionRid, _actorId,
            _timeout, ct);
    }

    public bool Submit(ReplyHandler callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        EnsureNotSubmitted();
        _submitted = true;
        return ActorInterop.UnbindActorCallback(_stream, _sessionRid, _actorId,
            _timeout, callback);
    }

    private void EnsureNotSubmitted()
    {
        if (_submitted)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
    }
}
