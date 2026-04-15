// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;

namespace Zlink;

internal delegate void ReceivedReplyHandler(IReadOnlyList<Message> parts,
    SendFlags flags);

public sealed class Received : IDisposable
{
    private readonly ReceivedReplyHandler? _replyHandler;
    private int _closed;

    internal Received(RoutingId? routingId, Message[] parts,
        ulong? requestSeq = null, RoutingId? spotRid = null,
        ReceivedReplyHandler? replyHandler = null)
    {
        RoutingId = routingId;
        SpotRid = spotRid;
        RequestSeq = requestSeq;
        Parts = parts ?? Array.Empty<Message>();
        _replyHandler = replyHandler;
    }

    public RoutingId? RoutingId { get; }

    public RoutingId? SpotRid { get; }

    public ulong? RequestSeq { get; }

    public IReadOnlyList<Message> Parts { get; }

    public bool IsSinglePart => Parts.Count == 1;

    public Message FirstPart()
    {
        if (Parts.Count == 0)
        {
            throw new ZlinkRecvException(RecvResult.NoData,
                (int)ErrorCode.EAgain);
        }

        return Parts[0];
    }

    public Message SinglePartOrThrow()
    {
        if (Parts.Count != 1)
        {
            throw new ZlinkRecvException(RecvResult.NotSupported,
                (int)ErrorCode.ENotSup);
        }

        return Parts[0];
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
        foreach (Message part in Parts)
            part.Dispose();
    }
}

public sealed record SubscriptionEvent(
    RoutingId? RoutingId,
    string? ServiceName,
    string Topic,
    bool Subscribed);
