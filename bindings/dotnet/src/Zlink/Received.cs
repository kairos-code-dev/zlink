// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections;
using System.Collections.Generic;
using System.Threading;

namespace Zlink;

internal delegate void ReceivedReplyHandler(IReadOnlyList<Message> parts,
    SendFlags flags);

public sealed class Received : IDisposable, IReadOnlyList<Message>
{
    private readonly ReceivedReplyHandler? _replyHandler;
    private readonly byte[]? _routingIdBytes;
    private readonly byte[]? _spotRidBytes;
    private readonly MultipartMessageCollection _parts;
    private int _closed;
    private RoutingId? _routingId;
    private RoutingId? _spotRid;

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
        _spotRid = spotRid;
        RequestSeq = requestSeq;
        _parts = parts ?? MultipartMessageCollection.FromMessages(Array.Empty<Message>());
        _replyHandler = replyHandler;
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
        _routingIdBytes = routingIdBytes;
        _spotRidBytes = spotRidBytes;
        RequestSeq = requestSeq;
        _parts = parts ?? MultipartMessageCollection.FromMessages(Array.Empty<Message>());
        _replyHandler = replyHandler;
    }

    internal Received(RoutingId? routingId, Message singlePart,
        ulong? requestSeq = null, RoutingId? spotRid = null,
        ReceivedReplyHandler? replyHandler = null) : this(routingId,
            MultipartMessageCollection.FromSingle(singlePart), requestSeq,
            spotRid, replyHandler)
    { }

    internal Received(byte[]? routingIdBytes, Message singlePart,
        bool adoptRoutingBytes, ulong? requestSeq = null,
        byte[]? spotRidBytes = null,
        ReceivedReplyHandler? replyHandler = null) : this(routingIdBytes,
            MultipartMessageCollection.FromSingle(singlePart), adoptRoutingBytes,
            requestSeq, spotRidBytes, replyHandler)
    { }

    public RoutingId? RoutingId
    {
        get
        {
            if (_routingIdBytes == null)
                return _routingId;
            _routingId ??= global::Zlink.RoutingId.FromOwnedOptionalBytes(
                _routingIdBytes);
            return _routingId;
        }
    }

    public RoutingId? SpotRid
    {
        get
        {
            if (_spotRidBytes == null)
                return _spotRid;
            _spotRid ??= global::Zlink.RoutingId.FromOwnedOptionalBytes(
                _spotRidBytes);
            return _spotRid;
        }
    }

    public ulong? RequestSeq { get; }

    public IReadOnlyList<Message> Parts => this;

    public int Count => _parts.Count;

    public Message this[int index] => _parts[index];

    public bool IsSinglePart => _parts.IsSinglePart;

    public Message FirstPart()
    {
        return _parts.First();
    }

    public Message SinglePartOrThrow()
    {
        return _parts.Single();
    }

    public void Reply(Message part)
    {
        Reply(part, SendFlags.None);
    }

    public void Reply(Message part, SendFlags flags)
    {
        if (part == null)
            throw new ArgumentNullException(nameof(part));
        Reply(new[] { part }, flags);
    }

    public void Reply(IReadOnlyList<Message> parts)
    {
        Reply(parts, SendFlags.None);
    }

    public void Reply(IReadOnlyList<Message> parts, SendFlags flags)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (RequestSeq is null || _replyHandler == null)
        {
            throw new ZlinkSubmitException(SubmitResult.InvalidArgument,
                (int)ErrorCode.EInval);
        }

        _replyHandler(parts, flags);
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _closed, 1) != 0)
            return;
        _parts.Dispose();
    }

    public IEnumerator<Message> GetEnumerator()
    {
        return _parts.GetEnumerator();
    }

    IEnumerator IEnumerable.GetEnumerator()
    {
        return GetEnumerator();
    }
}

public sealed record SubscriptionEvent(
    RoutingId? RoutingId,
    string? ServiceName,
    string Topic,
    bool Subscribed);
