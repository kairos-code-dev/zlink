// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

internal sealed class RouterReplyOperation : ReplyOperation,
    ReplySubmitOperation
{
    private readonly RoutingId _destNodeRid;
    private readonly RoutingId _destSpotRid;
    private readonly RouterOperationKind _kind;
    private readonly RoutingId _peerRid;
    private readonly ulong _requestSeq;
    private readonly RouterSocket _socket;
    private SendFlags _flags;
    private OperationMessageBuffer _parts;
    private OperationSubmissionGuard _submission;

    internal RouterReplyOperation(RouterSocket socket, RouterOperationKind kind,
        RoutingId peerRid, RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq)
    {
        _socket = socket;
        _kind = kind;
        _peerRid = peerRid;
        _destNodeRid = destNodeRid;
        _destSpotRid = destSpotRid;
        _requestSeq = requestSeq;
    }

    public ReplySubmitOperation Message(Message message)
    {
        EnsureNotSubmitted();
        _parts.Add(message);
        return this;
    }

    public ReplySubmitOperation Flags(SendFlags flags)
    {
        EnsureNotSubmitted();
        _flags = flags;
        return this;
    }

    public void Submit()
    {
        EnsureReady();
        _submission.MarkSubmitted();
        switch (_kind)
        {
            case RouterOperationKind.Reply:
                _socket.ReplyCore(_peerRid, _requestSeq, _parts.Parts, _flags);
                break;
            case RouterOperationKind.ReplyToSpot:
                _socket.ReplyToSpotCore(_destNodeRid, _destSpotRid, _requestSeq,
                    _parts.Parts, _flags);
                break;
            default:
                throw new ZlinkConfigException(
                    ZlinkConfigException.ErrorCode.InvalidState);
        }
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

internal sealed class ReceivedReplyOperationImpl : ReplyOperation,
    ReplySubmitOperation
{
    private readonly Received _received;
    private SendFlags _flags;
    private OperationMessageBuffer _parts;
    private OperationSubmissionGuard _submission;

    internal ReceivedReplyOperationImpl(Received received)
    {
        _received = received;
    }

    public ReplySubmitOperation Message(Message message)
    {
        EnsureNotSubmitted();
        _parts.Add(message);
        return this;
    }

    public ReplySubmitOperation Flags(SendFlags flags)
    {
        EnsureNotSubmitted();
        _flags = flags;
        return this;
    }

    public void Submit()
    {
        EnsureReady();
        _submission.MarkSubmitted();
        _received.ReplyCore(_parts.Parts, _flags);
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