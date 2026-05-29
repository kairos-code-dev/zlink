// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
/// Describes subscription entry data.
/// </summary>
/// <param name="Filter">The filter value.</param>
/// <param name="IsPattern">The is pattern value.</param>
public sealed record SubscriptionEntry(string Filter, bool IsPattern);

/// <summary>
/// Represents subscription event.
/// </summary>
public sealed class SubscriptionEvent
{
    /// <summary>
    /// Creates a subscription event instance.
    /// </summary>
    public SubscriptionEvent()
    {
    }

    internal SubscriptionEvent(RoutingId? routingId, string topic,
        bool subscribed)
    {
        Populate(routingId, topic, subscribed);
    }

    /// <summary>
    /// Gets or sets the routing id.
    /// </summary>
    public RoutingId? RoutingId { get; private set; }

    /// <summary>
    /// Gets or sets the topic.
    /// </summary>
    public string Topic { get; private set; } = string.Empty;

    /// <summary>
    /// Gets or sets the subscribed.
    /// </summary>
    public bool Subscribed { get; private set; }

    internal void Populate(RoutingId? routingId, string topic, bool subscribed)
    {
        RoutingId = routingId;
        Topic = topic ?? string.Empty;
        Subscribed = subscribed;
    }
}
