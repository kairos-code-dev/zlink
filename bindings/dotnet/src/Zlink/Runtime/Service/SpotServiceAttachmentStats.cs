// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed record SpotServiceAttachmentStats(
    string ChannelName,
    uint RouterCount,
    uint PubCount,
    uint SubCount,
    uint AutoRouterCount,
    uint AutoPubCount,
    uint AutoSubCount)
{
    internal static unsafe SpotServiceAttachmentStats FromNative(
        ref ZlinkSpotServiceAttachmentStats native)
    {
        fixed (byte* channelName = native.ChannelName)
        {
            return new SpotServiceAttachmentStats(
                NativeHelpers.ReadFixedString(channelName, 256),
                native.RouterCount, native.PubCount, native.SubCount,
                native.AutoRouterCount, native.AutoPubCount,
                native.AutoSubCount);
        }
    }
}
