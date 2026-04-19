// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;

namespace Zlink;

public sealed class TopicMessage : IDisposable
{
    private readonly MultipartMessageCollection _parts;
    private int _closed;

    internal TopicMessage(RoutingId? routingId, string? serviceName, string topic,
        Message[] parts)
        : this(routingId, serviceName, topic,
            MultipartMessageCollection.FromMessages(parts))
    {
    }

    internal TopicMessage(RoutingId? routingId, string? serviceName, string topic,
        MultipartMessageCollection parts)
    {
        RoutingId = routingId;
        ServiceName = serviceName;
        Topic = topic ?? string.Empty;
        _parts = parts ?? MultipartMessageCollection.FromMessages(Array.Empty<Message>());
    }

    public RoutingId? RoutingId { get; }

    public string? ServiceName { get; }

    public string Topic { get; }

    public IReadOnlyList<Message> Parts => _parts;

    public bool IsSinglePart => _parts.IsSinglePart;

    public Message FirstPart()
    {
        return _parts.First();
    }

    public Message SinglePartOrThrow()
    {
        return _parts.Single();
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _closed, 1) != 0)
            return;
        _parts.Dispose();
    }
}
