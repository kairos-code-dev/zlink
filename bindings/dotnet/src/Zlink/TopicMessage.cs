// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;

namespace Systems.Zlink;

public sealed class TopicMessage : IDisposable
{
    private MultipartMessageCollection? _parts;
    private int _closed;
    private RoutingId? _routingId;
    private string _topic = string.Empty;

    public TopicMessage()
    {
    }

    internal TopicMessage(RoutingId? routingId, string topic, Message[] parts)
        : this(routingId, topic,
            MultipartMessageCollection.FromMessages(parts))
    {
    }

    internal TopicMessage(RoutingId? routingId, string topic,
        MultipartMessageCollection parts)
    {
        Populate(routingId, topic, parts);
    }

    public RoutingId? RoutingId => _routingId;

    public string Topic => _topic;

    public IReadOnlyList<Message> Parts => PartsCollection;

    public bool IsSinglePart => PartsCollection.IsSinglePart;

    public Message FirstPart()
    {
        return PartsCollection.First();
    }

    public Message SinglePartOrThrow()
    {
        return PartsCollection.Single();
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _closed, 1) != 0)
            return;
        _parts?.Dispose();
        _parts = null;
    }

    internal void Populate(RoutingId? routingId, string topic,
        MultipartMessageCollection parts)
    {
        ResetForReuse();
        _routingId = routingId;
        _topic = topic ?? string.Empty;
        _parts = parts ?? MultipartMessageCollection.FromMessages(Array.Empty<Message>());
    }

    private void ResetForReuse()
    {
        _parts?.Dispose();
        _parts = null;
        _routingId = null;
        _topic = string.Empty;
        _closed = 0;
    }

    private MultipartMessageCollection PartsCollection
    {
        get
        {
            return _parts ??= MultipartMessageCollection.FromMessages(
                Array.Empty<Message>());
        }
    }
}
