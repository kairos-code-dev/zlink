// SPDX-License-Identifier: MPL-2.0

using System;
namespace Systems.Zlink;

public interface IMessageSocket : IConnectableSocket
{
    SendOperation Send();

    bool Recv(Received result, RecvFlags flags = RecvFlags.None);

    void OnSendReady(Action handler);
}

public interface IPairSocket : IMessageSocket
{
}

public interface IDealerSocket : IMessageSocket
{
    new DealerSocketOptions Options { get; }

    void SetRoutingId(RoutingId routingId);

    RoutingId GetRoutingId();

    void AttachDiscovery(IDiscovery discovery);

    new void SetChannelName(string channelName);

    string GetChannelName();

    RequestOperation Request();
}
