// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using Systems.Zlink.Native;
using Systems.Zlink.Sockets.Internal;

namespace Systems.Zlink;

public sealed class Received : IDisposable
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

    /// <summary>
    /// Create an empty <see cref="Received"/> for caller-provided storage.
    /// Hand the same instance to <c>Recv(Received, ...)</c> across calls to
    /// avoid the per-recv allocation; the binding overwrites the internal
    /// state on each successful receive.
    /// </summary>
    public Received()
    {
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

    public RoutingId? RoutingId
    {
        get
        {
            if (_routingIdSnapshot.HasValue)
                _routingId ??= _routingIdSnapshot.ToRoutingId();
            return _routingId;
        }
    }

    public RoutingId? SpotRid
    {
        get
        {
            return _metadata?.SpotRid;
        }
    }

    public ulong? RequestSeq => _metadata?.RequestSeq;

    public IReadOnlyList<Message> Parts => PartsCollection;

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

    public bool IsSinglePart => _singlePart != null || PartsCollection.IsSinglePart;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public Message FirstPart()
    {
        return _singlePart ?? PartsCollection.First();
    }

    public Message SinglePartOrThrow()
    {
        return _singlePart ?? PartsCollection.Single();
    }

    internal IReadOnlyList<Message> TakePartsOwnership()
    {
        if (_singlePart != null)
        {
            Message part = _singlePart;
            _singlePart = null;
            _closed = true;
            return new[] { part };
        }
        return PartsCollection.TakeMessages();
    }

    /// <summary>
    /// Start a reply (operation builder).
    /// </summary>
    public ReplyOperation Reply()
    {
        return new ReceivedReplyOperationImpl(this);
    }

    internal void ReplyCore(IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (_metadata is not { RequestSeq: { } requestSeq,
                ReplyHandler: { } replyHandler })
        {
            throw new ZlinkSubmitException(SubmitResult.InvalidArgument,
                (int)ErrorCode.EInval);
        }

        replyHandler(parts, flags);
    }

    /// <summary>
    /// Start a send (operation builder).
    /// </summary>
    public SendOperation Send()
    {
        return new ReceivedSendOperationImpl(this);
    }

    internal bool SendCore(Message part, SendFlags flags = SendFlags.None)
    {
        if (part == null)
            throw new ArgumentNullException(nameof(part));
        if (_sendKernel != null)
        {
            return _sendKernel.SendReceivedSingle(_sendRoutingIdSnapshot,
                _sendSpotRidSnapshot, part, flags);
        }
        if (_sendSingleHandler != null)
            return _sendSingleHandler(part, flags);
        return SendCore(new[] { part }, flags);
    }

    internal bool SendCore(IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 1)
            return SendCore(parts[0], flags);
        if (_sendKernel != null)
        {
            return _sendKernel.SendReceivedParts(_sendRoutingIdSnapshot,
                _sendSpotRidSnapshot, parts, flags);
        }
        if (_sendHandler == null)
        {
            throw new ZlinkSubmitException(SubmitResult.InvalidArgument,
                (int)ErrorCode.EInval);
        }

        return _sendHandler(parts, flags);
    }

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

    internal IEnumerator<Message> GetEnumerator()
    {
        return PartsCollection.GetEnumerator();
    }

    /// <summary>
    /// Reset internal state so the same Received can be reused for the next
    /// Recv call. Owned messages from the previous receive are disposed.
    /// </summary>
    internal void ResetForReuse()
    {
        if (_singlePart != null)
        {
            _singlePart.DisposeNativeOwned();
            _singlePart = null;
        }
        if (_parts != null)
        {
            _parts.Dispose();
            _parts = null;
        }
        _routingId = null;
        _routingIdSnapshot = default;
        _metadata = null;
        _sendSingleHandler = null;
        _sendHandler = null;
        _sendKernel = null;
        _sendRoutingIdSnapshot = default;
        _sendSpotRidSnapshot = default;
        _closed = false;
    }

    internal void PopulateSinglePart(Message singlePart)
    {
        ResetForReuse();
        _singlePart = singlePart;
    }

    internal void PopulateMultipart(MultipartMessageCollection parts)
    {
        ResetForReuse();
        _parts = parts;
    }

    internal void PopulateRoutedSinglePart(Message singlePart,
        RoutingIdSnapshot routingId, RoutingIdSnapshot spotRid,
        ulong? requestSeq, ReceivedReplyHandler? replyHandler,
        ReceivedSendHandler? sendHandler = null,
        ReceivedSendSingleHandler? sendSingleHandler = null,
        SocketKernel? sendKernel = null)
    {
        ResetForReuse();
        _singlePart = singlePart;
        _routingIdSnapshot = routingId;
        _metadata = ReceivedMetadata.Create(spotRid, requestSeq, replyHandler);
        _sendSingleHandler = sendSingleHandler;
        _sendHandler = sendHandler;
        SetSendContext(sendKernel, routingId, spotRid);
    }

    internal void PopulateRoutedMultipart(MultipartMessageCollection parts,
        RoutingIdSnapshot routingId, RoutingIdSnapshot spotRid,
        ulong? requestSeq, ReceivedReplyHandler? replyHandler,
        ReceivedSendHandler? sendHandler = null,
        ReceivedSendSingleHandler? sendSingleHandler = null,
        SocketKernel? sendKernel = null)
    {
        ResetForReuse();
        _parts = parts;
        _routingIdSnapshot = routingId;
        _metadata = ReceivedMetadata.Create(spotRid, requestSeq, replyHandler);
        _sendSingleHandler = sendSingleHandler;
        _sendHandler = sendHandler;
        SetSendContext(sendKernel, routingId, spotRid);
    }

    internal void SetSendHandler(ReceivedSendHandler? sendHandler,
        ReceivedSendSingleHandler? sendSingleHandler = null)
    {
        _sendKernel = null;
        _sendRoutingIdSnapshot = default;
        _sendSpotRidSnapshot = default;
        _sendSingleHandler = sendSingleHandler;
        _sendHandler = sendHandler;
    }

    internal void SetSendContext(SocketKernel? sendKernel,
        RoutingIdSnapshot routingId, RoutingIdSnapshot spotRid)
    {
        _sendKernel = sendKernel;
        _sendRoutingIdSnapshot = routingId;
        _sendSpotRidSnapshot = spotRid;
    }

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
}

public sealed class SubscriptionEvent
{
    public SubscriptionEvent()
    {
    }

    internal SubscriptionEvent(RoutingId? routingId, string topic,
        bool subscribed)
    {
        Populate(routingId, topic, subscribed);
    }

    public RoutingId? RoutingId { get; private set; }

    public string Topic { get; private set; } = string.Empty;

    public bool Subscribed { get; private set; }

    internal void Populate(RoutingId? routingId, string topic, bool subscribed)
    {
        RoutingId = routingId;
        Topic = topic ?? string.Empty;
        Subscribed = subscribed;
    }
}

public sealed record SubscriptionEntry(string Filter, bool IsPattern);
