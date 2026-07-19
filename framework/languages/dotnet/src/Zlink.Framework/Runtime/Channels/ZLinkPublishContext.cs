namespace Zlink.Framework.Runtime.Channels;

using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Streams;

internal sealed class ZLinkPublishContext : IZLinkHandlerContext
{
    internal ZLinkPublishContext(
        string meshName,
        string? channelName,
        string packetName,
        string? contentType,
        string topic,
        string? source,
        CancellationToken connectionAborted,
        ZLinkMessageMetadata? metadata = null)
    {
        MeshName = meshName;
        ChannelName = channelName;
        PacketName = packetName;
        ContentType = contentType;
        Topic = topic;
        Source = source;
        ConnectionAborted = connectionAborted;
        Metadata = metadata ?? ZLinkMessageMetadata.Empty;
    }

    public string MeshName { get; }

    public string? ChannelName { get; }

    public string PacketName { get; }

    public string? ContentType { get; }

    public ZLinkMessageMetadata Metadata { get; }

    public CancellationToken ConnectionAborted { get; }

    public string Topic { get; }

    public string? Source { get; }
}
