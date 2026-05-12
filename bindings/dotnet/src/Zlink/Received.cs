// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using Systems.Zlink.Native;

namespace Systems.Zlink;

internal delegate void ReceivedReplyHandler(IReadOnlyList<Message> parts,
    SendFlags flags);
internal delegate bool ReceivedSendSingleHandler(Message part, SendFlags flags);
internal delegate bool ReceivedSendHandler(IReadOnlyList<Message> parts,
    SendFlags flags);

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

    public void Reply(Message part, SendFlags flags = SendFlags.None)
    {
        if (part == null)
            throw new ArgumentNullException(nameof(part));
        Reply(new[] { part }, flags);
    }

    public void Reply(IReadOnlyList<Message> parts,
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

    public bool Send(Message part, SendFlags flags = SendFlags.None)
    {
        if (part == null)
            throw new ArgumentNullException(nameof(part));
        if (_sendSingleHandler != null)
            return _sendSingleHandler(part, flags);
        return Send(new[] { part }, flags);
    }

    public bool Send(IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
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
        ReceivedSendSingleHandler? sendSingleHandler = null)
    {
        ResetForReuse();
        _singlePart = singlePart;
        _routingIdSnapshot = routingId;
        _metadata = ReceivedMetadata.Create(spotRid, requestSeq, replyHandler);
        _sendSingleHandler = sendSingleHandler;
        _sendHandler = sendHandler;
    }

    internal void PopulateRoutedMultipart(MultipartMessageCollection parts,
        RoutingIdSnapshot routingId, RoutingIdSnapshot spotRid,
        ulong? requestSeq, ReceivedReplyHandler? replyHandler,
        ReceivedSendHandler? sendHandler = null,
        ReceivedSendSingleHandler? sendSingleHandler = null)
    {
        ResetForReuse();
        _parts = parts;
        _routingIdSnapshot = routingId;
        _metadata = ReceivedMetadata.Create(spotRid, requestSeq, replyHandler);
        _sendSingleHandler = sendSingleHandler;
        _sendHandler = sendHandler;
    }

    internal void SetSendHandler(ReceivedSendHandler? sendHandler,
        ReceivedSendSingleHandler? sendSingleHandler = null)
    {
        _sendSingleHandler = sendSingleHandler;
        _sendHandler = sendHandler;
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

public sealed record SubscriptionEvent(
    RoutingId? RoutingId,
    string Topic,
    bool Subscribed);

public sealed record SubscriptionEntry(string Filter, bool IsPattern);
