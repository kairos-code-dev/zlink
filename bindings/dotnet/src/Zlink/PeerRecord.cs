// SPDX-License-Identifier: MPL-2.0

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
}
