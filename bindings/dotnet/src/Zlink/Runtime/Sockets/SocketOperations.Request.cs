// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;

namespace Systems.Zlink;
internal sealed class DealerRequestOperation : RequestOperation,
    RequestSubmitOperation, RequestCallbackSubmitOperation
{
    private readonly DealerSocket _socket;
    private OperationMessageBuffer _parts;
    private TimeSpan _timeout;
    private SendFlags _flags;
    private bool _callbackStage;
    private OperationSubmissionGuard _submission;

    internal DealerRequestOperation(DealerSocket socket)
    {
        _socket = socket;
    }

    public RequestSubmitOperation Message(Message message)
    {
        AddMessage(message);
        return this;
    }

    RequestCallbackSubmitOperation RequestCallbackSubmitOperation.Message(
        Message message)
    {
        AddMessage(message);
        return this;
    }

    public RequestSubmitOperation Timeout(TimeSpan timeout)
    {
        EnsureNotSubmitted();
        _timeout = timeout;
        return this;
    }

    RequestCallbackSubmitOperation RequestCallbackSubmitOperation.Timeout(
        TimeSpan timeout)
    {
        EnsureNotSubmitted();
        _timeout = timeout;
        return this;
    }

    public RequestCallbackSubmitOperation Flags(SendFlags flags)
    {
        EnsureNotSubmitted();
        _callbackStage = true;
        _flags = flags;
        return this;
    }

    RequestCallbackSubmitOperation RequestCallbackSubmitOperation.Flags(
        SendFlags flags)
    {
        EnsureNotSubmitted();
        _flags = flags;
        return this;
    }

    public Task<IReadOnlyList<Message>> Async(
        CancellationToken ct = default)
    {
        EnsureReady();
        if (_callbackStage)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
        _submission.MarkSubmitted();
        return _socket.RequestCore(_parts.Parts, _timeout, ct);
    }

    public bool Submit(RequestCallback callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        EnsureReady();
        _submission.MarkSubmitted();
        return _socket.RequestCallbackCore(_parts.Parts, callback, _flags,
            _timeout);
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

internal enum RouterOperationKind
{
    Request,
    RequestToSpot,
    SendToSpot,
    Reply,
    ReplyToSpot
}

internal sealed class RouterRequestOperation : RequestOperation,
    RequestSubmitOperation, RequestCallbackSubmitOperation
{
    private readonly RouterSocket _socket;
    private readonly RouterOperationKind _kind;
    private readonly RoutingId _peerRid;
    private readonly RoutingId _destNodeRid;
    private readonly RoutingId _destSpotRid;
    private OperationMessageBuffer _parts;
    private TimeSpan _timeout;
    private SendFlags _flags;
    private bool _callbackStage;
    private OperationSubmissionGuard _submission;

    internal RouterRequestOperation(RouterSocket socket,
        RouterOperationKind kind, RoutingId peerRid, RoutingId destNodeRid,
        RoutingId destSpotRid)
    {
        _socket = socket;
        _kind = kind;
        _peerRid = peerRid;
        _destNodeRid = destNodeRid;
        _destSpotRid = destSpotRid;
    }

    public RequestSubmitOperation Message(Message message)
    {
        AddMessage(message);
        return this;
    }

    RequestCallbackSubmitOperation RequestCallbackSubmitOperation.Message(
        Message message)
    {
        AddMessage(message);
        return this;
    }

    public RequestSubmitOperation Timeout(TimeSpan timeout)
    {
        EnsureNotSubmitted();
        _timeout = timeout;
        return this;
    }

    RequestCallbackSubmitOperation RequestCallbackSubmitOperation.Timeout(
        TimeSpan timeout)
    {
        EnsureNotSubmitted();
        _timeout = timeout;
        return this;
    }

    public RequestCallbackSubmitOperation Flags(SendFlags flags)
    {
        EnsureNotSubmitted();
        _callbackStage = true;
        _flags = flags;
        return this;
    }

    RequestCallbackSubmitOperation RequestCallbackSubmitOperation.Flags(
        SendFlags flags)
    {
        EnsureNotSubmitted();
        _flags = flags;
        return this;
    }

    public Task<IReadOnlyList<Message>> Async(
        CancellationToken ct = default)
    {
        EnsureReady();
        if (_callbackStage)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
        _submission.MarkSubmitted();
        return _kind switch
        {
            RouterOperationKind.Request => _socket.RequestCore(_peerRid,
                _parts.Parts, _timeout, ct),
            RouterOperationKind.RequestToSpot => _socket.RequestToSpotCore(
                _destNodeRid, _destSpotRid, _parts.Parts, _timeout, ct),
            _ => throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState)
        };
    }

    public bool Submit(RequestCallback callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        EnsureReady();
        _submission.MarkSubmitted();
        return _kind switch
        {
            RouterOperationKind.Request => _socket.RequestCallbackCore(_peerRid,
                _parts.Parts, callback, _flags, _timeout),
            RouterOperationKind.RequestToSpot => _socket
                .RequestToSpotCallbackCore(_destNodeRid, _destSpotRid,
                    _parts.Parts, callback, _flags, _timeout),
            _ => throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState)
        };
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
