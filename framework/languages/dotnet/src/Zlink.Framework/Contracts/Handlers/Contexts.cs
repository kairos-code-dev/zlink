namespace Zlink.Framework.Contracts.Handlers;

using Zlink.Framework.Contracts.Streams;

public interface IZLinkHandlerContext
{
    string MeshName { get; }

    string? ChannelName { get; }

    string PacketName { get; }

    string? ContentType { get; }

    /// <summary>
    ///     The immutable application-metadata snapshot the sender attached;
    ///     <see cref="ZLinkMessageMetadata.Empty" /> when none was sent.
    /// </summary>
    ZLinkMessageMetadata Metadata { get; }

    CancellationToken ConnectionAborted { get; }
}

public sealed class ZLinkRequestContext : IZLinkHandlerContext
{
    internal ZLinkRequestContext(
        string meshName,
        string channelName,
        string packetName,
        string? contentType,
        CancellationToken connectionAborted,
        ZLinkMessageMetadata? metadata = null)
    {
        MeshName = meshName;
        ChannelName = channelName;
        PacketName = packetName;
        ContentType = contentType;
        ConnectionAborted = connectionAborted;
        Metadata = metadata ?? ZLinkMessageMetadata.Empty;
    }

    public string MeshName { get; }

    public string ChannelName { get; }

    public string PacketName { get; }

    public string? ContentType { get; }

    public ZLinkMessageMetadata Metadata { get; }

    public CancellationToken ConnectionAborted { get; }
}

public sealed class ZLinkSendContext : IZLinkHandlerContext
{
    internal ZLinkSendContext(
        string meshName,
        string channelName,
        string packetName,
        string? contentType,
        CancellationToken connectionAborted,
        ZLinkMessageMetadata? metadata = null)
    {
        MeshName = meshName;
        ChannelName = channelName;
        PacketName = packetName;
        ContentType = contentType;
        ConnectionAborted = connectionAborted;
        Metadata = metadata ?? ZLinkMessageMetadata.Empty;
    }

    public string MeshName { get; }

    public string ChannelName { get; }

    public string PacketName { get; }

    public string? ContentType { get; }

    public ZLinkMessageMetadata Metadata { get; }

    public CancellationToken ConnectionAborted { get; }
}
