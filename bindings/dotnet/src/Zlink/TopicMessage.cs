// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;

namespace Zlink;

public sealed class TopicMessage : IDisposable
{
    private int _closed;

    internal TopicMessage(RoutingId? routingId, string? serviceName, string topic,
        Message[] parts)
    {
        RoutingId = routingId;
        ServiceName = serviceName;
        Topic = topic ?? string.Empty;
        Parts = parts ?? Array.Empty<Message>();
    }

    public RoutingId? RoutingId { get; }

    public string? ServiceName { get; }

    public string Topic { get; }

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

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _closed, 1) != 0)
            return;
        foreach (Message part in Parts)
            part.Dispose();
    }
}
