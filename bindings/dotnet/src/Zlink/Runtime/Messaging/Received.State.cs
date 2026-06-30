// SPDX-License-Identifier: MPL-2.0

using System.Collections;
using Systems.Zlink.Runtime.Sockets.Internal;

namespace Systems.Zlink;

public sealed partial class Received
{
    private bool _closed;
    private ReceivedMetadata? _metadata;
    private MultipartMessageCollection? _parts;
    private RoutingId? _routingId;
    private RoutingIdSnapshot _routingIdSnapshot;
    private ReceivedSendHandler? _sendHandler;
    private SocketKernel? _sendKernel;
    private RoutingIdSnapshot _sendRoutingIdSnapshot;
    private ReceivedSendSingleHandler? _sendSingleHandler;
    private RoutingIdSnapshot _sendSpotRidSnapshot;
    private Message? _singlePart;

    private Received()
    {
    }

    internal Received(RoutingId? routingId, Message[] parts,
        ulong? requestSeq = null, RoutingId? spotRid = null,
        ReceivedReplyHandler? replyHandler = null)
        : this(routingId, MultipartMessageCollection.FromMessages(parts),
            requestSeq, spotRid, replyHandler)
    {
    }

    internal Received(RoutingId? routingId, MultipartMessageCollection parts,
        ulong? requestSeq = null, RoutingId? spotRid = null,
        ReceivedReplyHandler? replyHandler = null)
    {
        _routingId = routingId;
        _metadata = ReceivedMetadata.Create(spotRid, requestSeq, replyHandler);
        _parts = parts ?? MultipartMessageCollection.FromMessages(Array.Empty<Message>());
    }

    internal Received(byte[]? routingIdBytes, Message[] parts,
        bool adoptRoutingBytes, ulong? requestSeq = null,
        byte[]? spotRidBytes = null,
        ReceivedReplyHandler? replyHandler = null)
        : this(routingIdBytes, MultipartMessageCollection.FromMessages(parts),
            adoptRoutingBytes, requestSeq, spotRidBytes, replyHandler)
    {
    }

    internal Received(byte[]? routingIdBytes, MultipartMessageCollection parts,
        bool adoptRoutingBytes, ulong? requestSeq = null,
        byte[]? spotRidBytes = null,
        ReceivedReplyHandler? replyHandler = null)
    {
        _ = adoptRoutingBytes;
        _routingIdSnapshot = RoutingIdSnapshot.FromBytes(routingIdBytes);
        _metadata = ReceivedMetadata.Create(
            RoutingIdSnapshot.FromBytes(spotRidBytes), requestSeq, replyHandler);
        _parts = parts ?? MultipartMessageCollection.FromMessages(Array.Empty<Message>());
    }

    internal Received(RoutingId? routingId, Message singlePart,
        ulong? requestSeq = null, RoutingId? spotRid = null,
        ReceivedReplyHandler? replyHandler = null)
    {
        _routingId = routingId;
        _metadata = ReceivedMetadata.Create(spotRid, requestSeq, replyHandler);
        _singlePart = singlePart ?? throw new ArgumentNullException(nameof(singlePart));
    }

    internal Received(byte[]? routingIdBytes, Message singlePart,
        bool adoptRoutingBytes, ulong? requestSeq = null,
        byte[]? spotRidBytes = null,
        ReceivedReplyHandler? replyHandler = null)
    {
        _ = adoptRoutingBytes;
        _routingIdSnapshot = RoutingIdSnapshot.FromBytes(routingIdBytes);
        _metadata = ReceivedMetadata.Create(
            RoutingIdSnapshot.FromBytes(spotRidBytes), requestSeq, replyHandler);
        _singlePart = singlePart ?? throw new ArgumentNullException(nameof(singlePart));
    }

    internal Received(RoutingIdSnapshot routingId,
        Message singlePart, ulong? requestSeq = null,
        RoutingIdSnapshot spotRid = default,
        ReceivedReplyHandler? replyHandler = null)
    {
        _routingIdSnapshot = routingId;
        _metadata = ReceivedMetadata.Create(spotRid, requestSeq, replyHandler);
        _singlePart = singlePart ?? throw new ArgumentNullException(nameof(singlePart));
    }

    internal Received(RoutingIdSnapshot routingId,
        MultipartMessageCollection parts, ulong? requestSeq = null,
        RoutingIdSnapshot spotRid = default,
        ReceivedReplyHandler? replyHandler = null)
    {
        _routingIdSnapshot = routingId;
        _metadata = ReceivedMetadata.Create(spotRid, requestSeq, replyHandler);
        _parts = parts ?? MultipartMessageCollection.FromMessages(Array.Empty<Message>());
    }

    internal int Count => _singlePart != null ? 1 : PartsCollection.Count;

    internal Message this[int index]
    {
        get
        {
            if (_singlePart != null)
            {
                if (index == 0)
                    return _singlePart;
                throw new ArgumentOutOfRangeException(nameof(index));
            }

            return PartsCollection[index];
        }
    }

    private MultipartMessageCollection PartsCollection
    {
        get
        {
            if (_parts != null)
                return _parts;
            if (_singlePart == null)
                return _parts = MultipartMessageCollection.FromMessages(Array.Empty<Message>());
            var part = _singlePart;
            _singlePart = null;
            return _parts = MultipartMessageCollection.FromSingle(part);
        }
    }

    private void DisposeCore()
    {
        if (_closed)
            return;
        _closed = true;
        if (_singlePart != null)
        {
            _singlePart.DisposeNativeOwned();
            _singlePart = null;
            return;
        }

        _parts?.Dispose();
    }

    internal static Received Create(RoutingId? routingId, Message[] parts,
        ulong? requestSeq = null, RoutingId? spotRid = null,
        ReceivedReplyHandler? replyHandler = null)
    {
        return new Received(routingId, parts, requestSeq, spotRid, replyHandler);
    }

    internal static Received Create(RoutingId? routingId,
        MultipartMessageCollection parts, ulong? requestSeq = null,
        RoutingId? spotRid = null, ReceivedReplyHandler? replyHandler = null)
    {
        return new Received(routingId, parts, requestSeq, spotRid, replyHandler);
    }

    internal static Received Create(byte[]? routingIdBytes, Message[] parts,
        bool adoptRoutingBytes, ulong? requestSeq = null,
        byte[]? spotRidBytes = null,
        ReceivedReplyHandler? replyHandler = null)
    {
        return new Received(routingIdBytes, parts, adoptRoutingBytes,
            requestSeq, spotRidBytes, replyHandler);
    }

    internal static Received Create(byte[]? routingIdBytes,
        MultipartMessageCollection parts, bool adoptRoutingBytes,
        ulong? requestSeq = null, byte[]? spotRidBytes = null,
        ReceivedReplyHandler? replyHandler = null)
    {
        return new Received(routingIdBytes, parts, adoptRoutingBytes,
            requestSeq, spotRidBytes, replyHandler);
    }

    internal static Received Create(RoutingId? routingId, Message singlePart,
        ulong? requestSeq = null, RoutingId? spotRid = null,
        ReceivedReplyHandler? replyHandler = null)
    {
        return new Received(routingId, singlePart, requestSeq, spotRid,
            replyHandler);
    }

    internal static Received Create(byte[]? routingIdBytes, Message singlePart,
        bool adoptRoutingBytes, ulong? requestSeq = null,
        byte[]? spotRidBytes = null,
        ReceivedReplyHandler? replyHandler = null)
    {
        return new Received(routingIdBytes, singlePart, adoptRoutingBytes,
            requestSeq, spotRidBytes, replyHandler);
    }

    internal static Received Create(RoutingIdSnapshot routingId,
        Message singlePart, ulong? requestSeq = null,
        RoutingIdSnapshot spotRid = default,
        ReceivedReplyHandler? replyHandler = null)
    {
        return new Received(routingId, singlePart, requestSeq, spotRid,
            replyHandler);
    }

    internal static Received Create(RoutingIdSnapshot routingId,
        MultipartMessageCollection parts, ulong? requestSeq = null,
        RoutingIdSnapshot spotRid = default,
        ReceivedReplyHandler? replyHandler = null)
    {
        return new Received(routingId, parts, requestSeq, spotRid,
            replyHandler);
    }

    internal IReadOnlyList<Message> TakePartsOwnership()
    {
        if (_singlePart != null)
        {
            var part = _singlePart;
            _singlePart = null;
            _closed = true;
            return new SingleMessageList(part);
        }

        return PartsCollection.TakeMessages();
    }

    internal IEnumerator<Message> GetEnumerator()
    {
        return PartsCollection.GetEnumerator();
    }

    private sealed class ReceivedMetadata
    {
        private RoutingId? _spotRid;
        private RoutingIdSnapshot _spotRidSnapshot;

        private ReceivedMetadata(RoutingId? spotRid, ulong? requestSeq,
            ReceivedReplyHandler? replyHandler)
        {
            _spotRid = spotRid;
            RequestSeq = requestSeq;
            ReplyHandler = replyHandler;
        }

        private ReceivedMetadata(RoutingIdSnapshot spotRid, ulong? requestSeq,
            ReceivedReplyHandler? replyHandler)
        {
            _spotRidSnapshot = spotRid;
            RequestSeq = requestSeq;
            ReplyHandler = replyHandler;
        }

        internal ulong? RequestSeq { get; }

        internal ReceivedReplyHandler? ReplyHandler { get; }

        internal RoutingId? SpotRid
        {
            get
            {
                if (_spotRidSnapshot.HasValue)
                    _spotRid ??= _spotRidSnapshot.ToRoutingId();
                return _spotRid;
            }
        }

        internal static ReceivedMetadata? Create(RoutingId? spotRid,
            ulong? requestSeq, ReceivedReplyHandler? replyHandler)
        {
            return spotRid.HasValue || requestSeq.HasValue || replyHandler != null
                ? new ReceivedMetadata(spotRid, requestSeq, replyHandler)
                : null;
        }

        internal static ReceivedMetadata? Create(RoutingIdSnapshot spotRid,
            ulong? requestSeq, ReceivedReplyHandler? replyHandler)
        {
            return spotRid.HasValue || requestSeq.HasValue || replyHandler != null
                ? new ReceivedMetadata(spotRid, requestSeq, replyHandler)
                : null;
        }
    }

    private sealed class SingleMessageList : IReadOnlyList<Message>
    {
        private readonly Message _message;

        internal SingleMessageList(Message message)
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

        IEnumerator IEnumerable
            .GetEnumerator()
        {
            return GetEnumerator();
        }
    }
}