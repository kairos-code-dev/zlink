namespace Zlink.Framework.Contracts.Streams;

public sealed class ZLinkSessionDispatchContext
{
    public ZLinkSessionDispatchContext(
        string packetName,
        ZLinkMessageMetadata? metadata = null,
        bool canReply = false)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(packetName);
        PacketName = packetName;
        Metadata = metadata ?? ZLinkMessageMetadata.Empty;
        CanReply = canReply;
    }

    internal ZLinkSessionDispatchContext(ZlinkStreamHeader header)
    {
        Header = header;
        PacketName = header.Name;
        Metadata = header.Metadata.Count == 0
            ? ZLinkMessageMetadata.Empty
            : new ZLinkMessageMetadata(new Dictionary<string, string>(header.Metadata.Values, StringComparer.Ordinal));
        CanReply = header.RequestSeq.HasValue;
    }

    public string PacketName { get; }

    public ZLinkMessageMetadata Metadata { get; }

    public bool CanReply { get; }

    internal ZlinkStreamHeader? Header { get; }
}
