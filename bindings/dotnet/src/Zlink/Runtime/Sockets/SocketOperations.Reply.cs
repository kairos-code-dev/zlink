// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

internal abstract class RouterReplyOperation : ReplyOperation,
    ReplySubmitOperation
{
    private OperationMessageBuffer _parts;
    private OperationSubmissionGuard _submission;

    public ReplySubmitOperation Message(Message message)
    {
        EnsureNotSubmitted();
        _parts.Add(message);
        return this;
    }

    public void Submit()
    {
        EnsureReady();
        _submission.MarkSubmittedAfterValidation();
        SubmitCore(_parts.Parts);
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

    protected abstract void SubmitCore(IReadOnlyList<Message> parts);
}

internal sealed class RouterPeerReplyOperation : RouterReplyOperation
{
    private readonly RoutingId _peerRid;
    private readonly ulong _requestSeq;
    private readonly RouterSocket _socket;

    internal RouterPeerReplyOperation(
        RouterSocket socket,
        RoutingId peerRid,
        ulong requestSeq)
    {
        _socket = socket;
        _peerRid = peerRid;
        _requestSeq = requestSeq;
    }

    protected override void SubmitCore(IReadOnlyList<Message> parts)
    {
        _socket.ReplyCore(_peerRid, _requestSeq, parts);
    }
}

internal sealed class ReceivedReplyOperationImpl : ReplyOperation,
    ReplySubmitOperation
{
    private readonly Received _received;
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

    public void Submit()
    {
        EnsureReady();
        _submission.MarkSubmittedAfterValidation();
        _received.ReplyCore(_parts.Parts);
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
