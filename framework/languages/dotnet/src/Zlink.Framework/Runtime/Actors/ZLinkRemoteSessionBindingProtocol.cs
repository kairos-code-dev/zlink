namespace Zlink.Framework.Runtime.Actors;

internal static class ZLinkRemoteSessionBindingProtocol
{
    public const string PacketName = "framework.internal.actor-session-bind";
}

[ZLinkPacket(ZLinkRemoteSessionBindingProtocol.PacketName)]
internal sealed record ZLinkRemoteSessionBindRequest(
    byte[] SessionNodeRid,
    byte[] SessionRid);

internal sealed record ZLinkRemoteSessionBindResponse(bool Acknowledged);
