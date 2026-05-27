// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;

namespace Systems.Zlink;

public interface IMessageSocket : IConnectableSocket
{
    SendOperation Send();

    bool Send(Message message, SendFlags flags = SendFlags.None);

    bool RecvPart(Message result, out bool hasMore,
        RecvFlags flags = RecvFlags.None);

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

    bool RequestFrame(ulong requestSeq, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None);

    void Reply(ulong requestToken, IReadOnlyList<Message> parts);
}
