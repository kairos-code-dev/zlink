// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;

namespace Systems.Zlink.Runtime.Native;

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkInstanceSpotPlacement
{
    public ZlinkRoutingId NodeRid;
    public ulong NodeGeneration;
    public ZlinkRoutingId SpotRid;
    public IntPtr InstanceSpotType;
    public nuint InstanceSpotTypeSize;
    public IntPtr MessageContractId;
    public nuint MessageContractIdSize;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkInstanceSpotActivationToken
{
    public fixed ulong Opaque[4];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkInstanceSpotActivationData
{
    public ZlinkRoutingId SpotRid;
    public int OperationKind;
    public fixed byte InstanceSpotType[256];
    public fixed byte MessageContractId[256];
    public ZlinkInstanceSpotActivationToken Token;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkInstanceSpotClaimResult
{
    public int Role;
    public IntPtr LeaderSpot;
    public ulong LeaderSpotGeneration;
}
