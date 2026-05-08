// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Systems.Zlink;

public interface SendOperation
{
    SendSubmitOperation Message(Message message);
}

public interface SendSubmitOperation
{
    SendSubmitOperation Message(Message message);
    SendSubmitOperation Flags(SendFlags flags);
    bool Submit();
}

public interface RequestOperation
{
    RequestSubmitOperation Message(Message message);
}

public interface RequestSubmitOperation
{
    RequestSubmitOperation Message(Message message);
    RequestSubmitOperation Timeout(TimeSpan timeout);
    RequestCallbackSubmitOperation Flags(SendFlags flags);
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    bool Submit(RequestCallback callback);
}

public interface RequestCallbackSubmitOperation
{
    RequestCallbackSubmitOperation Message(Message message);
    RequestCallbackSubmitOperation Timeout(TimeSpan timeout);
    RequestCallbackSubmitOperation Flags(SendFlags flags);
    bool Submit(RequestCallback callback);
}

public interface ReplyOperation
{
    ReplySubmitOperation Message(Message message);
}

public interface ReplySubmitOperation
{
    ReplySubmitOperation Message(Message message);
    ReplySubmitOperation Flags(SendFlags flags);
    void Submit();
}

internal enum SpotOperationKind
{
    Publish,
    SendChannel,
    SendToSpot,
    RequestChannel,
    RequestToSpot,
    RequestToRouter,
    ReplyToSpot,
    ReplyToRouter
}

internal sealed class SpotSendOperation : SendOperation, SendSubmitOperation
{
    private readonly Spot _spot;
    private readonly SpotOperationKind _kind;
    private readonly string? _serviceName;
    private readonly string? _topicOrChannel;
    private readonly RoutingId _destNodeRid;
    private readonly RoutingId _destSpotRid;
    private readonly List<Message> _parts = new();
    private SendFlags _flags;
    private bool _submitted;

    internal SpotSendOperation(Spot spot, SpotOperationKind kind,
        string? serviceName = null, string? topicOrChannel = null,
        RoutingId destNodeRid = default, RoutingId destSpotRid = default)
    {
        _spot = spot;
        _kind = kind;
        _serviceName = serviceName;
        _topicOrChannel = topicOrChannel;
        _destNodeRid = destNodeRid;
        _destSpotRid = destSpotRid;
    }

    public SendSubmitOperation Message(Message message)
    {
        EnsureNotSubmitted();
        if (message == null)
            throw new ArgumentNullException(nameof(message));
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
            SpotOperationKind.Publish => _spot.Publish(_serviceName!,
                _topicOrChannel!, _parts, _flags),
            SpotOperationKind.SendChannel => _spot.SendChannel(_topicOrChannel!,
                _parts, _flags),
            SpotOperationKind.SendToSpot => _spot.SendToSpot(_destNodeRid,
                _destSpotRid, _parts, _flags),
            _ => throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState)
        };
    }

    private void EnsureReadyToSubmit()
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

internal sealed class SpotRequestOperation : RequestOperation,
    RequestSubmitOperation, RequestCallbackSubmitOperation
{
    private readonly Spot _spot;
    private readonly SpotOperationKind _kind;
    private readonly string? _channelName;
    private readonly RoutingId _destNodeRid;
    private readonly RoutingId _destSpotRid;
    private readonly RoutingId _peerRid;
    private readonly List<Message> _parts = new();
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
            SpotOperationKind.RequestChannel => _spot.RequestChannelAsync(
                _channelName!, _parts, _timeout, ct),
            SpotOperationKind.RequestToSpot => _spot.RequestToSpotAsync(
                _destNodeRid, _destSpotRid, _parts, _timeout, ct),
            SpotOperationKind.RequestToRouter => _spot.RequestToRouterAsync(
                _peerRid, _parts, _timeout, ct),
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
            SpotOperationKind.RequestChannel => _spot.RequestChannel(
                _channelName!, _parts, (result, parts) => callback(result, parts),
                _flags, _timeout),
            SpotOperationKind.RequestToSpot => _spot.RequestToSpot(_destNodeRid,
                _destSpotRid, _parts, (result, parts) => callback(result, parts),
                _flags, _timeout),
            SpotOperationKind.RequestToRouter => _spot.RequestToRouter(_peerRid,
                _parts, (result, parts) => callback(result, parts), _flags,
                _timeout),
            _ => throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState)
        };
    }

    private void AddMessage(Message message)
    {
        EnsureNotSubmitted();
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        _parts.Add(message);
    }

    private void EnsureReadyToSubmit()
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

internal sealed class SpotReplyOperation : ReplyOperation, ReplySubmitOperation
{
    private readonly Spot _spot;
    private readonly SpotOperationKind _kind;
    private readonly RoutingId _destNodeRid;
    private readonly RoutingId _destSpotRid;
    private readonly RoutingId _peerRid;
    private readonly ulong _requestSeq;
    private readonly List<Message> _parts = new();
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
        if (message == null)
            throw new ArgumentNullException(nameof(message));
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
                    _parts, _flags);
                break;
            case SpotOperationKind.ReplyToRouter:
                _spot.ReplyToRouter(_peerRid, _requestSeq, _parts, _flags);
                break;
            default:
                throw new ZlinkConfigException(
                    ZlinkConfigException.ErrorCode.InvalidState);
        }
    }

    private void EnsureReadyToSubmit()
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
