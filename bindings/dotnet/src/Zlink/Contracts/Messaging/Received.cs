// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using Systems.Zlink.Native;
using Systems.Zlink.Sockets.Internal;

namespace Systems.Zlink;

/// <summary>
/// Describes the envelope kind of a received message.
/// </summary>
public enum ReceivedMessageType
{
    Raw = 0,
    Request = 1,
    Reply = 2,
    ErrorReply = 3
}

/// <summary>
/// Holds one received message envelope.
/// </summary>
/// <remarks>
/// A received envelope owns its message parts until disposed or until an API
/// explicitly transfers ownership. Reuse instances created by <c>Create</c>
/// with receive APIs that accept caller-provided storage.
/// </remarks>
public sealed partial class Received : IDisposable
{
    private ReceivedMetadata? _metadata;
    private RoutingIdSnapshot _routingIdSnapshot;
    private MultipartMessageCollection? _parts;
    private Message? _singlePart;
    private bool _closed;
    private RoutingId? _routingId;
    private ReceivedSendSingleHandler? _sendSingleHandler;
    private ReceivedSendHandler? _sendHandler;
    private SocketKernel? _sendKernel;
    private RoutingIdSnapshot _sendRoutingIdSnapshot;
    private RoutingIdSnapshot _sendSpotRidSnapshot;

    private Received()
    {
    }

    /// <summary>
    /// Create an empty <see cref="Received"/> for caller-provided storage.
    /// Hand the same instance to <c>Recv(Received, ...)</c> across calls to
    /// avoid the per-recv allocation; the binding overwrites the internal
    /// state on each successful receive.
    /// </summary>
    public static Received Create()
    {
        return new Received();
    }

    /// <summary>
    /// Gets the source routing id when the receive path provides one.
    /// </summary>
    public RoutingId? RoutingId
    {
        get
        {
            if (_routingIdSnapshot.HasValue)
                _routingId ??= _routingIdSnapshot.ToRoutingId();
            return _routingId;
        }
    }

    /// <summary>
    /// Gets the source spot routing id when the envelope came from a spot route.
    /// </summary>
    public RoutingId? SpotRid
    {
        get
        {
            return _metadata?.SpotRid;
        }
    }

    /// <summary>
    /// Gets the request sequence when this envelope can be replied to.
    /// </summary>
    public ulong? RequestSeq => _metadata?.RequestSeq;

    /// <summary>
    /// Gets the envelope kind.
    /// </summary>
    public ReceivedMessageType MessageType { get; private set; } =
        ReceivedMessageType.Raw;

    /// <summary>
    /// Gets the message parts owned by this envelope.
    /// </summary>
    /// <remarks>
    /// The returned messages are disposed when this envelope is disposed unless
    /// ownership has been explicitly transferred by another API.
    /// </remarks>
    public IReadOnlyList<Message> Parts => PartsCollection;

    /// <summary>
    /// Gets whether the envelope currently contains exactly one message part.
    /// </summary>
    public bool IsSinglePart => _singlePart != null || PartsCollection.IsSinglePart;

    /// <summary>
    /// Returns the first message part without transferring ownership.
    /// </summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public Message FirstPart()
    {
        return _singlePart ?? PartsCollection.First();
    }

    /// <summary>
    /// Returns the only message part or throws when the envelope is multipart.
    /// </summary>
    public Message SinglePartOrThrow()
    {
        return _singlePart ?? PartsCollection.Single();
    }

    /// <summary>
    /// Start a reply operation for request envelopes.
    /// </summary>
    /// <remarks>
    /// The operation is valid only when <see cref="RequestSeq"/> has a value.
    /// </remarks>
    public ReplyOperation Reply()
    {
        return new ReceivedReplyOperationImpl(this);
    }

    /// <summary>
    /// Start a send operation addressed to the source route of this envelope.
    /// </summary>
    public SendOperation Send()
    {
        return new ReceivedSendOperationImpl(this);
    }

    /// <summary>
    /// Disposes the message parts owned by this envelope.
    /// </summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public void Dispose()
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

    internal IReadOnlyList<Message> TakePartsOwnership()
    {
        if (_singlePart != null)
        {
            Message part = _singlePart;
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

    /// <summary>
    /// Reset internal state so the same Received can be reused for the next
    /// Recv call. Owned messages from the previous receive are disposed.
    /// </summary>
    private MultipartMessageCollection PartsCollection
    {
        get
        {
            if (_parts != null)
                return _parts;
            if (_singlePart == null)
                return _parts = MultipartMessageCollection.FromMessages(Array.Empty<Message>());
            Message part = _singlePart;
            _singlePart = null;
            return _parts = MultipartMessageCollection.FromSingle(part);
        }
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

        System.Collections.IEnumerator System.Collections.IEnumerable
            .GetEnumerator()
        {
            return GetEnumerator();
        }
    }
}
