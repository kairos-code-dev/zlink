// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;

namespace Zlink;

public sealed class Received
{
    internal Received(string routingId, Message[] parts)
    {
        RoutingId = routingId ?? string.Empty;
        Parts = parts ?? Array.Empty<Message>();
    }

    public string RoutingId { get; }

    public RoutingId? RoutingIdValue =>
        string.IsNullOrEmpty(RoutingId) ? null : new RoutingId(RoutingId);

    public IReadOnlyList<Message> Parts { get; }

    public bool HasSinglePart => Parts.Count == 1;

    public Message SinglePartOrThrow()
    {
        if (Parts.Count != 1)
        {
            throw new InvalidOperationException(
                $"Expected exactly one message part but found {Parts.Count}.");
        }

        return Parts[0];
    }
}

public sealed class Subscribed : TopicMessage
{
    internal Subscribed(string routingId, string topic, Message[] parts)
        : base(routingId, topic, parts)
    {
    }
}

public sealed class SubscriptionEvent
{
    internal SubscriptionEvent(string routingId, string topic, bool subscribed)
    {
        RoutingId = routingId ?? string.Empty;
        Topic = topic ?? string.Empty;
        Subscribed = subscribed;
    }

    public string RoutingId { get; }

    public RoutingId? RoutingIdValue =>
        string.IsNullOrEmpty(RoutingId) ? null : new RoutingId(RoutingId);

    public string Topic { get; }

    public bool Subscribed { get; }
}
