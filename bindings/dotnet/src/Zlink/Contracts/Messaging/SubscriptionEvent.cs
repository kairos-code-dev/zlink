// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public sealed record SubscriptionEntry(string Filter, bool IsPattern);

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
