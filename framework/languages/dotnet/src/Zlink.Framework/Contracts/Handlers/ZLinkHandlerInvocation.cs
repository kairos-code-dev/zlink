namespace Zlink.Framework.Contracts.Handlers;

public sealed class ZLinkHandlerInvocation
{
    internal ZLinkHandlerInvocation(
        string meshName,
        string ownerKind,
        string packetName,
        ZLinkMessageMetadata metadata)
    {
        MeshName = meshName;
        OwnerKind = ownerKind;
        PacketName = packetName;
        Metadata = metadata;
    }

    public string MeshName { get; }

    public string OwnerKind { get; }

    public string PacketName { get; }

    public ZLinkMessageMetadata Metadata { get; }
}
