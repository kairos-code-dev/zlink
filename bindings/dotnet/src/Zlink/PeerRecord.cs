// SPDX-License-Identifier: MPL-2.0

using System.Buffers.Binary;
using Zlink.Native;

namespace Zlink;

public readonly struct PeerRecord
{
    public PeerRecord(string routingId, uint? streamRoutingId,
        string remoteAddress,
        ulong connectedTime, ulong msgsSent, ulong msgsReceived,
        ulong sndPendingMsgs, ulong rcvPendingMsgs)
    {
        RoutingId = routingId;
        StreamRoutingId = streamRoutingId;
        RemoteAddress = remoteAddress;
        ConnectedTime = connectedTime;
        MsgsSent = msgsSent;
        MsgsReceived = msgsReceived;
        SndPendingMsgs = sndPendingMsgs;
        RcvPendingMsgs = rcvPendingMsgs;
    }

    public string RoutingId { get; }
    public uint? StreamRoutingId { get; }
    public string RemoteAddress { get; }
    public ulong ConnectedTime { get; }
    public ulong MsgsSent { get; }
    public ulong MsgsReceived { get; }
    public ulong SndPendingMsgs { get; }
    public ulong RcvPendingMsgs { get; }

    internal static unsafe PeerRecord FromNative(ref ZlinkPeerInfo info)
    {
        byte[] routing = NativeHelpers.ReadRoutingId(ref info.RoutingId);
        string routingId = RoutingIdCodec.ToPublicString(routing);
        uint? streamRoutingId = routing.Length == sizeof(uint)
            ? BinaryPrimitives.ReadUInt32BigEndian(routing)
            : null;
        string remote;
        fixed (byte* ptr = info.RemoteAddr)
        {
            remote = NativeHelpers.ReadString(ptr, 256);
        }
        return new PeerRecord(routingId, streamRoutingId, remote,
            info.ConnectedTime,
            info.MsgsSent, info.MsgsReceived, info.SndPendingMsgs,
            info.RcvPendingMsgs);
    }
}
