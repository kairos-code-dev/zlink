// SPDX-License-Identifier: MPL-2.0

using Zlink.Native;

namespace Zlink;

public readonly struct PeerInfoRecord
{
    public PeerInfoRecord(byte[] routingId, string remoteAddress,
        ulong connectedTime, ulong msgsSent, ulong msgsReceived,
        ulong sndPendingMsgs, ulong rcvPendingMsgs)
    {
        RoutingId = routingId;
        RemoteAddress = remoteAddress;
        ConnectedTime = connectedTime;
        MsgsSent = msgsSent;
        MsgsReceived = msgsReceived;
        SndPendingMsgs = sndPendingMsgs;
        RcvPendingMsgs = rcvPendingMsgs;
    }

    public byte[] RoutingId { get; }
    public string RemoteAddress { get; }
    public ulong ConnectedTime { get; }
    public ulong MsgsSent { get; }
    public ulong MsgsReceived { get; }
    public ulong SndPendingMsgs { get; }
    public ulong RcvPendingMsgs { get; }

    internal static unsafe PeerInfoRecord FromNative(ref ZlinkPeerInfo info)
    {
        byte[] routing = NativeHelpers.ReadRoutingId(ref info.RoutingId);
        string remote;
        fixed (byte* ptr = info.RemoteAddr)
        {
            remote = NativeHelpers.ReadString(ptr, 256);
        }
        return new PeerInfoRecord(routing, remote, info.ConnectedTime,
            info.MsgsSent, info.MsgsReceived, info.SndPendingMsgs,
            info.RcvPendingMsgs);
    }
}
