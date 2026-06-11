// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Native;

namespace Systems.Zlink;

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
    private OperationMessageBuffer _parts;
    private TimeSpan _timeout;
    private SendFlags _flags;
    private bool _callbackStage;
    private OperationSubmissionGuard _submission;

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
        Async(CancellationToken ct = default)
    {
        EnsureReady();
        if (_callbackStage)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
        _submission.MarkSubmitted();
        return ActorInterop.JoinActorAsync(_node, _actor, _destNodeRid,
            _destSpotRid, _parts.Parts, _timeout, _flags, ct);
    }

    public bool Submit(ActorJoinHandler callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        EnsureReady();
        _submission.MarkSubmitted();
        return ActorInterop.JoinActorCallback(_node, _actor, _destNodeRid,
            _destSpotRid, _parts.Parts, _timeout, _flags, callback);
    }

    private void AddMessage(Message message)
    {
        EnsureNotSubmitted();
        _parts.Add(message);
    }

    private void EnsureReady()
    {
        EnsureNotSubmitted();
        _parts.EnsureNotEmpty();
    }

    private void EnsureNotSubmitted()
    {
        _submission.EnsureNotSubmitted();
    }
}

internal sealed class ActorJoinEntrySpotOperationImpl :
    ActorJoinEntrySpotOperation
{
    private readonly SpotNode _node;
    private readonly ActorRef _actor;
    private readonly RoutingId _destNodeRid;
    private TimeSpan _timeout;
    private OperationSubmissionGuard _submission;

    internal ActorJoinEntrySpotOperationImpl(SpotNode node, ActorRef actor,
        RoutingId destNodeRid)
    {
        _node = node;
        _actor = actor;
        _destNodeRid = destNodeRid;
    }

    public ActorJoinEntrySpotOperation Timeout(TimeSpan timeout)
    {
        EnsureNotSubmitted();
        _timeout = timeout;
        return this;
    }

    public Task<ActorJoinEntrySpotResult> Async(
        CancellationToken ct = default)
    {
        EnsureNotSubmitted();
        _submission.MarkSubmitted();
        return ActorInterop.JoinActorEntrySpotAsync(_node, _actor,
            _destNodeRid, _timeout, ct);
    }

    public bool Submit(ActorJoinEntrySpotHandler callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        EnsureNotSubmitted();
        _submission.MarkSubmitted();
        return ActorInterop.JoinActorEntrySpotCallback(_node, _actor,
            _destNodeRid, _timeout, callback);
    }

    private void EnsureNotSubmitted()
    {
        _submission.EnsureNotSubmitted();
    }
}

internal sealed class ActorJoinReplyOperationImpl : ActorJoinReplyOperation
{
    private readonly Spot _spot;
    private readonly ActorJoinRequest _request;
    private readonly int _joinResultCode;
    private OperationMessageBuffer _parts;
    private OperationSubmissionGuard _submission;

    internal ActorJoinReplyOperationImpl(Spot spot, ActorJoinRequest request,
        int joinResultCode)
    {
        _spot = spot;
        _request = request;
        _joinResultCode = joinResultCode;
    }

    public ActorJoinReplyOperation Message(Message message)
    {
        _submission.EnsureNotSubmitted();
        _parts.Add(message);
        return this;
    }

    public void Submit()
    {
        _submission.EnsureNotSubmitted();
        _submission.MarkSubmitted();
        _spot.ReplyActorJoinInternal(_request, _joinResultCode,
            _parts.PartsOrEmpty);
    }
}

internal sealed class ActorLeaveOperationImpl : ActorLeaveOperation
{
    private readonly SpotNode _node;
    private readonly ActorRef _actor;
    private readonly RoutingId _currentSpotRid;
    private TimeSpan _timeout;
    private OperationSubmissionGuard _submission;

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

    public Task<IReadOnlyList<Message>> Async(
        CancellationToken ct = default)
    {
        EnsureNotSubmitted();
        _submission.MarkSubmitted();
        return ActorInterop.LeaveActorAsync(_node, _actor, _currentSpotRid,
            _timeout, ct);
    }

    public bool Submit(ReplyHandler callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        EnsureNotSubmitted();
        _submission.MarkSubmitted();
        return ActorInterop.LeaveActorCallback(_node, _actor, _currentSpotRid,
            _timeout, callback);
    }

    private void EnsureNotSubmitted()
    {
        _submission.EnsureNotSubmitted();
    }
}

internal sealed class ActorDestroyOperationImpl : ActorDestroyOperation
{
    private readonly SpotNode _node;
    private readonly ActorRef _actor;
    private TimeSpan _timeout;
    private OperationSubmissionGuard _submission;

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

    public Task<IReadOnlyList<Message>> Async(
        CancellationToken ct = default)
    {
        EnsureNotSubmitted();
        _submission.MarkSubmitted();
        return ActorInterop.DestroyActorAsync(_node, _actor, _timeout, ct);
    }

    public bool Submit(ReplyHandler callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        EnsureNotSubmitted();
        _submission.MarkSubmitted();
        return ActorInterop.DestroyActorCallback(_node, _actor, _timeout,
            callback);
    }

    private void EnsureNotSubmitted()
    {
        _submission.EnsureNotSubmitted();
    }
}

internal sealed class ActorLookupOperationImpl : ActorLookupOperation
{
    private readonly SpotNode _node;
    private readonly RoutingId _targetNodeRid;
    private readonly string _actorId;
    private TimeSpan _timeout;
    private OperationSubmissionGuard _submission;

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

    public Task<ActorLookupResult> Async(CancellationToken ct = default)
    {
        EnsureNotSubmitted();
        _submission.MarkSubmitted();
        return ActorInterop.RemoteActorGetRefAsync(_node, _targetNodeRid,
            _actorId, _timeout, ct);
    }

    public bool Submit(ActorLookupHandler callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        EnsureNotSubmitted();
        _submission.MarkSubmitted();
        return ActorInterop.RemoteActorGetRefCallback(_node, _targetNodeRid,
            _actorId, _timeout, callback);
    }

    private void EnsureNotSubmitted()
    {
        _submission.EnsureNotSubmitted();
    }
}

internal sealed class ActorBindOperationImpl : ActorBindOperation
{
    private readonly StreamSocket _stream;
    private readonly RoutingId _sessionRid;
    private readonly ActorRef _actor;
    private TimeSpan _timeout;
    private OperationSubmissionGuard _submission;

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

    public Task<IReadOnlyList<Message>> Async(
        CancellationToken ct = default)
    {
        EnsureNotSubmitted();
        _submission.MarkSubmitted();
        return ActorInterop.BindActorAsync(_stream, _sessionRid, _actor,
            _timeout, ct);
    }

    public bool Submit(ReplyHandler callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        EnsureNotSubmitted();
        _submission.MarkSubmitted();
        return ActorInterop.BindActorCallback(_stream, _sessionRid, _actor,
            _timeout, callback);
    }

    private void EnsureNotSubmitted()
    {
        _submission.EnsureNotSubmitted();
    }
}

internal sealed class ActorUnbindOperationImpl : ActorUnbindOperation
{
    private readonly StreamSocket _stream;
    private readonly RoutingId _sessionRid;
    private readonly string _actorId;
    private TimeSpan _timeout;
    private OperationSubmissionGuard _submission;

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

    public Task<IReadOnlyList<Message>> Async(
        CancellationToken ct = default)
    {
        EnsureNotSubmitted();
        _submission.MarkSubmitted();
        return ActorInterop.UnbindActorAsync(_stream, _sessionRid, _actorId,
            _timeout, ct);
    }

    public bool Submit(ReplyHandler callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        EnsureNotSubmitted();
        _submission.MarkSubmitted();
        return ActorInterop.UnbindActorCallback(_stream, _sessionRid, _actorId,
            _timeout, callback);
    }

    private void EnsureNotSubmitted()
    {
        _submission.EnsureNotSubmitted();
    }
}
