// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;

namespace Zlink;

public class TopicMessage
{
    internal TopicMessage(string routingId, string topic, Message[] parts)
    {
        RoutingId = routingId ?? string.Empty;
        Topic = topic ?? string.Empty;
        Parts = parts ?? Array.Empty<Message>();
    }

    public string RoutingId { get; }

    public RoutingId? RoutingIdValue =>
        string.IsNullOrEmpty(RoutingId) ? null : new RoutingId(RoutingId);

    public string Topic { get; }

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
