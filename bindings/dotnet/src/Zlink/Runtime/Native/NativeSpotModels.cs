using System.Runtime.InteropServices;

namespace Systems.Zlink.Runtime.Native;

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkSpotRoute
{
    public ZlinkRoutingId SpotRid;
    public ZlinkRoutingId OwnerNodeRid;
    public int SpotKind;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkSpotServiceAttachmentStats
{
    public fixed byte ChannelName[256];
    public uint RouterCount;
    public uint PubCount;
    public uint SubCount;
    public uint AutoRouterCount;
    public uint AutoPubCount;
    public uint AutoSubCount;
}
