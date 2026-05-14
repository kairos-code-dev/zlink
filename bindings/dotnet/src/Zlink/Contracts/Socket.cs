// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
/// STREAM callback for framed packet dispatch.
/// Message ownership is transferred to the callback.
/// The callback must dispose each message exactly once.
/// </summary>
public delegate void StreamPacketHandler(RoutingId routingId, Message header,
    Message body);
/// <summary>
/// STREAM callback for framed packet dispatch with public string routing id.
/// Message ownership is transferred to the callback.
/// The callback must dispose each message exactly once.
/// </summary>
internal delegate void StreamFramedPacketHandler(
    string routingId,
    Message header,
    Message body);
internal delegate int StreamRawPacketHandler(string routingId, Message payload);
internal delegate int StreamUInt32PacketHandler(uint routingId, Message payload);
internal delegate void StreamUInt32FramedPacketHandler(uint routingId,
    Message header, Message body);
internal delegate void SocketRecvHandler(string routingId, Message[] parts);
internal delegate void SocketSubscribeHandler(string routingId, string topic,
    Message[] parts);
