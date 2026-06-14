// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal static class RoutingIdInterop
{
    internal static RoutingId? FromNative(ref ZlinkRoutingId routingId)
        => RoutingId.FromNative(ref routingId);
}
