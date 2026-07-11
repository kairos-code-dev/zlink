// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

internal enum SpotOperationKind
{
    Publish,
    SendToChannel,
    SendToSpot,
    RequestToChannel,
    RequestToSpot,
    RequestToRouter,
    ReplyToSpot,
    ReplyToRouter
}

internal sealed class SpotSendOperation : SendOperation, SendSubmitOperation
{
    private readonly RoutingId _destNodeRid;
    private readonly RoutingId _destSpotRid;
    private readonly SpotOperationKind _kind;
    private readonly Spot _spot;
    private readonly string? _channelName;
    private readonly string? _topic;
    private SendFlags _flags;
    private OperationMessageBuffer _parts;
    private OperationSubmissionGuard _submission;

    internal SpotSendOperation(Spot spot, SpotOperationKind kind,
        string? topic = null, string? channelName = null,
        RoutingId destNodeRid = default, RoutingId destSpotRid = default)
    {
        _spot = spot;
        _kind = kind;
        _topic = topic;
        _channelName = channelName;
        _destNodeRid = destNodeRid;
        _destSpotRid = destSpotRid;
    }

    public SendSubmitOperation Message(Message message)
    {
        EnsureNotSubmitted();
        _parts.Add(message);
        return this;
    }

    public SendSubmitOperation Flags(SendFlags flags)
    {
        EnsureNotSubmitted();
        _flags = flags;
        return this;
    }

    public bool Submit()
    {
        EnsureReadyToSubmit();
        _submission.MarkSubmittedAfterValidation();
        return _kind switch
        {
            SpotOperationKind.Publish => _parts.IsSingle
                ? _spot.Publish(_topic!, _parts.Single, _flags)
                : _spot.Publish(_topic!, _parts.Parts, _flags),
            SpotOperationKind.SendToChannel => _parts.IsSingle
                ? _spot.SendToChannel(_channelName!, _parts.Single, _flags)
                : _spot.SendToChannel(_channelName!, _parts.Parts, _flags),
            SpotOperationKind.SendToSpot => _parts.IsSingle
                ? _spot.SendToSpot(_destNodeRid, _destSpotRid, _parts.Single,
                    _flags)
                : _spot.SendToSpot(_destNodeRid, _destSpotRid, _parts.Parts,
                    _flags),
            _ => throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState)
        };
    }

    private void EnsureReadyToSubmit()
    {
        EnsureNotSubmitted();
        _parts.EnsureNotEmpty();
    }

    private void EnsureNotSubmitted()
    {
        _submission.EnsureNotSubmitted();
    }
}

internal sealed class SpotRequestOperation : RequestOperation,
    RequestSubmitOperation, RequestCallbackSubmitOperation
{
    private readonly string? _channelName;
    private readonly RoutingId _destNodeRid;
    private readonly RoutingId _destSpotRid;
    private readonly SpotOperationKind _kind;
    private readonly RoutingId _peerRid;
    private readonly Spot _spot;
    private bool _callbackStage;
    private SendFlags _flags;
    private OperationMessageBuffer _parts;
    private OperationSubmissionGuard _submission;
    private TimeSpan _timeout;

    internal SpotRequestOperation(Spot spot, SpotOperationKind kind,
        string? channelName = null, RoutingId destNodeRid = default,
        RoutingId destSpotRid = default, RoutingId peerRid = default)
    {
        _spot = spot;
        _kind = kind;
        _channelName = channelName;
        _destNodeRid = destNodeRid;
        _destSpotRid = destSpotRid;
        _peerRid = peerRid;
    }

    RequestCallbackSubmitOperation RequestCallbackSubmitOperation.Message(
        Message message)
    {
        AddMessage(message);
        return this;
    }

    RequestCallbackSubmitOperation RequestCallbackSubmitOperation.Timeout(
        TimeSpan timeout)
    {
        EnsureNotSubmitted();
        _timeout = timeout;
        return this;
    }

    public RequestSubmitOperation Message(Message message)
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

    public RequestCallbackSubmitOperation Flags(SendFlags flags)
    {
        EnsureNotSubmitted();
        _callbackStage = true;
        _flags = flags;
        return this;
    }

    public Task<IReadOnlyList<Message>> Async(
        CancellationToken ct = default)
    {
        EnsureReadyToSubmit();
        if (_callbackStage)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
        _submission.MarkSubmittedAfterValidation();
        return _kind switch
        {
            SpotOperationKind.RequestToChannel => _spot.RequestToChannelAsync(
                _channelName!, _parts.Parts, _timeout, ct),
            SpotOperationKind.RequestToSpot => _spot.RequestToSpotAsync(
                _destNodeRid, _destSpotRid, _parts.Parts, _timeout, ct),
            SpotOperationKind.RequestToRouter => _spot.RequestToRouterAsync(
                _peerRid, _parts.Parts, _timeout, ct),
            _ => throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState)
        };
    }

    public bool Submit(RequestCallback callback)
    {
        EnsureReadyToSubmit();
        _submission.MarkSubmittedAfterValidation();
        return _kind switch
        {
            SpotOperationKind.RequestToChannel => _spot.RequestToChannel(
                _channelName!, _parts.Parts,
                (result, parts) => callback(result, parts), _flags, _timeout),
            SpotOperationKind.RequestToSpot => _spot.RequestToSpot(_destNodeRid,
                _destSpotRid, _parts.Parts,
                (result, parts) => callback(result, parts), _flags, _timeout),
            SpotOperationKind.RequestToRouter => _spot.RequestToRouter(_peerRid,
                _parts.Parts, (result, parts) => callback(result, parts),
                _flags, _timeout),
            _ => throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState)
        };
    }

    private void AddMessage(Message message)
    {
        EnsureNotSubmitted();
        _parts.Add(message);
    }

    private void EnsureReadyToSubmit()
    {
        EnsureNotSubmitted();
        _parts.EnsureNotEmpty();
    }

    private void EnsureNotSubmitted()
    {
        _submission.EnsureNotSubmitted();
    }
}

internal sealed class SpotReplyOperation : ReplyOperation, ReplySubmitOperation
{
    private readonly RoutingId _destNodeRid;
    private readonly RoutingId _destSpotRid;
    private readonly SpotOperationKind _kind;
    private readonly RoutingId _peerRid;
    private readonly ulong _requestSeq;
    private readonly Spot _spot;
    private OperationMessageBuffer _parts;
    private OperationSubmissionGuard _submission;

    internal SpotReplyOperation(Spot spot, SpotOperationKind kind,
        RoutingId destNodeRid = default, RoutingId destSpotRid = default,
        RoutingId peerRid = default, ulong requestSeq = 0)
    {
        _spot = spot;
        _kind = kind;
        _destNodeRid = destNodeRid;
        _destSpotRid = destSpotRid;
        _peerRid = peerRid;
        _requestSeq = requestSeq;
    }

    public ReplySubmitOperation Message(Message message)
    {
        EnsureNotSubmitted();
        _parts.Add(message);
        return this;
    }

    public void Submit()
    {
        EnsureReadyToSubmit();
        _submission.MarkSubmittedAfterValidation();
        switch (_kind)
        {
            case SpotOperationKind.ReplyToSpot:
                _spot.ReplyToSpot(_destNodeRid, _destSpotRid, _requestSeq,
                    _parts.Parts);
                break;
            case SpotOperationKind.ReplyToRouter:
                _spot.ReplyToRouter(_peerRid, _requestSeq, _parts.Parts);
                break;
            default:
                throw new ZlinkConfigException(
                    ZlinkConfigException.ErrorCode.InvalidState);
        }
    }

    private void EnsureReadyToSubmit()
    {
        EnsureNotSubmitted();
        _parts.EnsureNotEmpty();
    }

    private void EnsureNotSubmitted()
    {
        _submission.EnsureNotSubmitted();
    }
}
