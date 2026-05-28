// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

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
    private readonly Spot _spot;
    private readonly SpotOperationKind _kind;
    private readonly string? _channelName;
    private readonly string? _topicOrChannel;
    private readonly RoutingId _destNodeRid;
    private readonly RoutingId _destSpotRid;
    private OperationMessageBuffer _parts;
    private SendFlags _flags;
    private bool _submitted;

    internal SpotSendOperation(Spot spot, SpotOperationKind kind,
        string? channelName = null, string? topicOrChannel = null,
        RoutingId destNodeRid = default, RoutingId destSpotRid = default)
    {
        _spot = spot;
        _kind = kind;
        _channelName = channelName;
        _topicOrChannel = topicOrChannel;
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
        _submitted = true;
        return _kind switch
        {
            SpotOperationKind.Publish => _parts.IsSingle
                ? _spot.Publish(_topicOrChannel!, _parts.Single, _flags)
                : _spot.Publish(_topicOrChannel!, _parts.Parts, _flags),
            SpotOperationKind.SendToChannel => _parts.IsSingle
                ? _spot.SendToChannel(_topicOrChannel!, _parts.Single, _flags)
                : _spot.SendToChannel(_topicOrChannel!, _parts.Parts, _flags),
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
        if (_submitted)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
    }
}

internal sealed class SpotRequestOperation : RequestOperation,
    RequestSubmitOperation, RequestCallbackSubmitOperation
{
    private readonly Spot _spot;
    private readonly SpotOperationKind _kind;
    private readonly string? _channelName;
    private readonly RoutingId _destNodeRid;
    private readonly RoutingId _destSpotRid;
    private readonly RoutingId _peerRid;
    private OperationMessageBuffer _parts;
    private TimeSpan _timeout;
    private SendFlags _flags;
    private bool _callbackStage;
    private bool _submitted;

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

    public Task<IReadOnlyList<Message>> SubmitAsync(
        CancellationToken ct = default)
    {
        EnsureReadyToSubmit();
        if (_callbackStage)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
        _submitted = true;
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
        _submitted = true;
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
        if (_submitted)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
    }
}

internal sealed class SpotReplyOperation : ReplyOperation, ReplySubmitOperation
{
    private readonly Spot _spot;
    private readonly SpotOperationKind _kind;
    private readonly RoutingId _destNodeRid;
    private readonly RoutingId _destSpotRid;
    private readonly RoutingId _peerRid;
    private readonly ulong _requestSeq;
    private OperationMessageBuffer _parts;
    private SendFlags _flags;
    private bool _submitted;

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

    public ReplySubmitOperation Flags(SendFlags flags)
    {
        EnsureNotSubmitted();
        _flags = flags;
        return this;
    }

    public void Submit()
    {
        EnsureReadyToSubmit();
        _submitted = true;
        switch (_kind)
        {
            case SpotOperationKind.ReplyToSpot:
                _spot.ReplyToSpot(_destNodeRid, _destSpotRid, _requestSeq,
                    _parts.Parts, _flags);
                break;
            case SpotOperationKind.ReplyToRouter:
                _spot.ReplyToRouter(_peerRid, _requestSeq, _parts.Parts,
                    _flags);
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
        if (_submitted)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
    }
}
