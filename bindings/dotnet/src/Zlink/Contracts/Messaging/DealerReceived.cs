// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace Systems.Zlink;

public sealed class DealerReceived : IDisposable, IAsyncDisposable
{
    private readonly DealerReplyHandler? _replyHandler;
    private bool _closed;

    internal DealerReceived(
        DealerMessageType messageType,
        ulong requestSeq,
        Message[] parts,
        DealerReplyHandler? replyHandler)
    {
        MessageType = messageType;
        RequestSeq = requestSeq;
        Parts = parts ?? Array.Empty<Message>();
        _replyHandler = replyHandler;
    }

    public DealerMessageType MessageType { get; }

    public ulong RequestSeq { get; }

    public IReadOnlyList<Message> Parts { get; }

    public Message FirstPart()
    {
        if (Parts.Count == 0)
        {
            throw new ZlinkRecvException(RecvResult.NoData,
                (int)ErrorCode.EAgain);
        }

        return Parts[0];
    }

    public void Reply(IReadOnlyList<Message> parts)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (_replyHandler == null)
        {
            throw new ZlinkSubmitException(SubmitResult.InvalidArgument,
                (int)ErrorCode.EInval);
        }

        _replyHandler(RequestSeq, parts);
    }

    public void Dispose()
    {
        if (_closed)
            return;
        _closed = true;
        foreach (Message part in Parts)
            part.Dispose();
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }
}

internal delegate void DealerReplyHandler(ulong requestToken,
    IReadOnlyList<Message> parts);
