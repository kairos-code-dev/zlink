// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

public interface IPublisherSocket : IConnectableSocket
{
    SendOperation Publish(string topic);

    void OnSendReady(Action handler);
}

public interface ISubscriberSocket : IConnectableSocket
{
    void SetSubscription(string topicOrPattern);

    void UnsetSubscription(string topicOrPattern);

    SubscriptionEntry? SubscriptionAt(int index);

    bool Subscribe(TopicMessage result, RecvFlags flags = RecvFlags.None);
}

public interface IPubSocket : IPublisherSocket
{
    new PubSocketOptions Options { get; }

    void AttachDiscovery(IDiscovery discovery);
}

public interface ISubSocket : ISubscriberSocket
{
    new SubSocketOptions Options { get; }

    void AttachDiscovery(IDiscovery discovery);
}

public interface IXPubSocket : IPublisherSocket
{
    new PubSocketOptions Options { get; }

    bool ReceiveSubscriptionEvent(SubscriptionEvent result,
        RecvFlags flags = RecvFlags.None);
}

public interface IXSubSocket : ISubscriberSocket
{
    new SubSocketOptions Options { get; }
}
