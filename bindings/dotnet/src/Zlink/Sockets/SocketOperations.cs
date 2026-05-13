// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Systems.Zlink;

// --- Raw socket operation builder implementations ---

internal enum RawSocketSendKind
{
    MessageSend,
    Publish,
    RoutedSend,
    StreamSendByRoutingId,
    StreamSendBoundActor,
    ActorSendBoundSession,
    ReceivedSend
}

internal struct OperationMessageBuffer
{
    private Message? _singlePart;
    private List<Message>? _parts;

    internal int Count => _parts?.Count ?? (_singlePart == null ? 0 : 1);

    internal bool IsSingle => _singlePart != null;

    internal Message Single => _singlePart
        ?? throw new ZlinkConfigException(
            ZlinkConfigException.ErrorCode.InvalidState);

    internal IReadOnlyList<Message> Parts =>
        _parts != null ? _parts : new SingleMessageReadOnlyList(Single);

    internal IReadOnlyList<Message> PartsOrEmpty =>
        Count == 0 ? Array.Empty<Message>() : Parts;

    internal void Add(Message message)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        if (_parts != null)
        {
            _parts.Add(message);
            return;
        }
        if (_singlePart == null)
        {
            _singlePart = message;
            return;
        }
        _parts = new List<Message> { _singlePart, message };
        _singlePart = null;
    }

    internal void EnsureNotEmpty()
    {
        if (Count == 0)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidArgument);
    }

    private sealed class SingleMessageReadOnlyList : IReadOnlyList<Message>
    {
        private readonly Message _message;

        internal SingleMessageReadOnlyList(Message message)
        {
            _message = message;
        }

        public int Count => 1;

        public Message this[int index]
        {
            get
            {
                if (index != 0)
                    throw new ArgumentOutOfRangeException(nameof(index));
                return _message;
            }
        }

        public IEnumerator<Message> GetEnumerator()
        {
            yield return _message;
        }

        System.Collections.IEnumerator System.Collections.IEnumerable
            .GetEnumerator()
        {
            return GetEnumerator();
        }
    }
}

internal sealed class MessageSocketSendOperation : SendOperation,
    SendSubmitOperation
{
    private readonly MessageSocketBase _socket;
    private OperationMessageBuffer _parts;
    private SendFlags _flags;
    private bool _submitted;

    internal MessageSocketSendOperation(MessageSocketBase socket)
    {
        _socket = socket;
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
        EnsureReady();
        _submitted = true;
        return _parts.IsSingle
            ? _socket.SendCore(_parts.Single, _flags)
            : _socket.SendCore(_parts.Parts, _flags);
    }

    private void EnsureReady()
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

internal sealed class PublisherSendOperation : SendOperation, SendSubmitOperation
{
    private readonly PublisherSocketBase _socket;
    private readonly string _topic;
    private OperationMessageBuffer _parts;
    private SendFlags _flags;
    private bool _submitted;

    internal PublisherSendOperation(PublisherSocketBase socket, string topic)
    {
        _socket = socket;
        _topic = topic;
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
        EnsureReady();
        _submitted = true;
        return _parts.IsSingle
            ? _socket.PublishCore(_topic, _parts.Single, _flags)
            : _socket.PublishCore(_topic, _parts.Parts, _flags);
    }

    private void EnsureReady()
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

internal sealed class RoutedSendOperation : SendOperation, SendSubmitOperation
{
    private readonly RoutedMessageSocketBase _socket;
    private readonly RoutingId _routingId;
    private OperationMessageBuffer _parts;
    private SendFlags _flags;
    private bool _submitted;

    internal RoutedSendOperation(RoutedMessageSocketBase socket,
        RoutingId routingId)
    {
        _socket = socket;
        _routingId = routingId;
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
        EnsureReady();
        _submitted = true;
        return _parts.IsSingle
            ? _socket.SendRoutedCore(_routingId, _parts.Single, _flags)
            : _socket.SendRoutedCore(_routingId, _parts.Parts, _flags);
    }

    private void EnsureReady()
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

internal sealed class DealerRequestOperation : RequestOperation,
    RequestSubmitOperation, RequestCallbackSubmitOperation
{
    private readonly DealerSocket _socket;
    private OperationMessageBuffer _parts;
    private TimeSpan _timeout;
    private SendFlags _flags;
    private bool _callbackStage;
    private bool _submitted;

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

    public Task<IReadOnlyList<Message>> SubmitAsync(
        CancellationToken ct = default)
    {
        EnsureReady();
        if (_callbackStage)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
        _submitted = true;
        return _socket.RequestCore(_parts.Parts, _timeout, ct);
    }

    public bool Submit(RequestCallback callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        EnsureReady();
        _submitted = true;
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
        if (_submitted)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
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
    private bool _submitted;

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

    public Task<IReadOnlyList<Message>> SubmitAsync(
        CancellationToken ct = default)
    {
        EnsureReady();
        if (_callbackStage)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
        _submitted = true;
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
        _submitted = true;
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
        if (_submitted)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
    }
}

internal sealed class RouterSendOperation : SendOperation, SendSubmitOperation
{
    private readonly RouterSocket _socket;
    private readonly RoutingId _destNodeRid;
    private readonly RoutingId _destSpotRid;
    private OperationMessageBuffer _parts;
    private SendFlags _flags;
    private bool _submitted;

    internal RouterSendOperation(RouterSocket socket, RoutingId destNodeRid,
        RoutingId destSpotRid)
    {
        _socket = socket;
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
        EnsureReady();
        _submitted = true;
        return _socket.SendToSpotCore(_destNodeRid, _destSpotRid, _parts.Parts,
            _flags);
    }

    private void EnsureReady()
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

internal sealed class RouterReplyOperation : ReplyOperation,
    ReplySubmitOperation
{
    private readonly RouterSocket _socket;
    private readonly RouterOperationKind _kind;
    private readonly RoutingId _peerRid;
    private readonly RoutingId _destNodeRid;
    private readonly RoutingId _destSpotRid;
    private readonly ulong _requestSeq;
    private OperationMessageBuffer _parts;
    private SendFlags _flags;
    private bool _submitted;

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
        _submitted = true;
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
        if (_submitted)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidState);
    }
}

internal sealed class StreamSendOperation : SendOperation, SendSubmitOperation
{
    private readonly StreamSocket _socket;
    private readonly RoutingId? _routingId;
    private readonly RoutingId? _sessionRid;
    private readonly string? _actorId;
    private OperationMessageBuffer _parts;
    private SendFlags _flags;
    private bool _submitted;

    internal StreamSendOperation(StreamSocket socket, RoutingId routingId)
    {
        _socket = socket;
        _routingId = routingId;
    }

    internal StreamSendOperation(StreamSocket socket, RoutingId sessionRid,
        string actorId)
    {
        _socket = socket;
        _sessionRid = sessionRid;
        _actorId = actorId;
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
        EnsureReady();
        _submitted = true;
        if (_actorId != null)
        {
            return _socket.SendBoundActorCore(_sessionRid!.Value, _actorId,
                _parts.Parts, _flags);
        }
        return _parts.IsSingle
            ? _socket.SendRoutedCore(_routingId!.Value, _parts.Single, _flags)
            : _socket.SendRoutedCore(_routingId!.Value, _parts.Parts, _flags);
    }

    private void EnsureReady()
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

internal sealed class ActorSendBoundSessionOperation : SendOperation,
    SendSubmitOperation
{
    private readonly SpotNode _node;
    private readonly ActorRef _actor;
    private OperationMessageBuffer _parts;
    private SendFlags _flags;
    private bool _submitted;

    internal ActorSendBoundSessionOperation(SpotNode node, ActorRef actor)
    {
        _node = node;
        _actor = actor;
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
        EnsureReady();
        _submitted = true;
        return Actor.SendBoundSessionCore(_node, _actor, _parts.Parts, _flags);
    }

    private void EnsureReady()
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

internal sealed class ReceivedSendOperationImpl : SendOperation,
    SendSubmitOperation
{
    private readonly Received _received;
    private OperationMessageBuffer _parts;
    private SendFlags _flags;
    private bool _submitted;

    internal ReceivedSendOperationImpl(Received received)
    {
        _received = received;
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
        EnsureReady();
        _submitted = true;
        return _parts.IsSingle
            ? _received.SendCore(_parts.Single, _flags)
            : _received.SendCore(_parts.Parts, _flags);
    }

    private void EnsureReady()
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

internal sealed class ReceivedReplyOperationImpl : ReplyOperation,
    ReplySubmitOperation
{
    private readonly Received _received;
    private OperationMessageBuffer _parts;
    private SendFlags _flags;
    private bool _submitted;

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
        _submitted = true;
        _received.ReplyCore(_parts.Parts, _flags);
    }

    private void EnsureReady()
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
