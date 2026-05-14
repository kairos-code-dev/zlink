// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
/// STREAM callback for framed packet dispatch.
/// Message ownership is transferred to the callback.
/// The callback must dispose each message exactly once.
/// </summary>
public delegate void StreamPacketHandler(RoutingId routingId, Message header,
    Message body);
