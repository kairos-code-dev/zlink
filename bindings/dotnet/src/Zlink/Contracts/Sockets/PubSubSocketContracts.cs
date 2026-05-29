// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Defines the publisher socket contract.
/// </summary>
public interface IPublisherSocket : IConnectableSocket
{
    /// <summary>
    /// Gets or sets the publish.
    /// </summary>
    SendOperation Publish(string topic);

    /// <summary>
    /// Registers a handler for send ready.
    /// </summary>
    void OnSendReady(Action handler);
}

/// <summary>
/// Defines the subscriber socket contract.
/// </summary>
public interface ISubscriberSocket : IConnectableSocket
{
    /// <summary>
    /// Sets the subscription.
    /// </summary>
    void SetSubscription(string topicOrPattern);

    /// <summary>
    /// Unsets the subscription.
    /// </summary>
    void UnsetSubscription(string topicOrPattern);

    /// <summary>
    /// Gets or sets the subscription at.
    /// </summary>
    SubscriptionEntry? SubscriptionAt(int index);

    /// <summary>
    /// Gets or sets the subscribe.
    /// </summary>
    bool Subscribe(TopicMessage result, RecvFlags flags = RecvFlags.None);
}

/// <summary>
/// Defines the pub socket contract.
/// </summary>
public interface IPubSocket : IPublisherSocket
{
    /// <summary>
    /// Gets or sets the options.
    /// </summary>
    new PubSocketOptions Options { get; }

    /// <summary>
    /// Gets or sets the attach discovery.
    /// </summary>
    void AttachDiscovery(IDiscovery discovery);
}

/// <summary>
/// Defines the sub socket contract.
/// </summary>
public interface ISubSocket : ISubscriberSocket
{
    /// <summary>
    /// Gets or sets the options.
    /// </summary>
    new SubSocketOptions Options { get; }

    /// <summary>
    /// Gets or sets the attach discovery.
    /// </summary>
    void AttachDiscovery(IDiscovery discovery);
}

/// <summary>
/// Defines the xpub socket contract.
/// </summary>
public interface IXPubSocket : IPublisherSocket
{
    /// <summary>
    /// Gets or sets the options.
    /// </summary>
    new PubSocketOptions Options { get; }

    /// <summary>
    /// Receives the next available item.
    /// </summary>
    bool ReceiveSubscriptionEvent(SubscriptionEvent result,
        RecvFlags flags = RecvFlags.None);
}

/// <summary>
/// Defines the xsub socket contract.
/// </summary>
public interface IXSubSocket : ISubscriberSocket
{
    /// <summary>
    /// Gets or sets the options.
    /// </summary>
    new SubSocketOptions Options { get; }
}
