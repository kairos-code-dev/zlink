namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamSendBuilder<TMessage>(TMessage message)
{
    private static readonly IZlinkStreamPacketNameResolver MessageNameResolver =
        ZLinkStreamProtocolDefaults.PacketNameResolver;

    private string _messageName = MessageNameResolver.Resolve(typeof(TMessage));
    private ZlinkStreamMetadata _metadata = ZlinkStreamMetadata.Empty;
    private bool _compress;
    private int _executed;

    public void AddMetadata(string key, string value)
    {
        _metadata = _metadata.With(key, value);
    }

    public void SetPacketName(string messageName)
    {
        if (string.IsNullOrWhiteSpace(messageName))
        {
            throw new InvalidOperationException("Stream packet name must not be empty.");
        }

        _messageName = messageName;
    }

    public void EnableCompression()
    {
        _compress = true;
    }

    public void Write(
        Func<ZlinkStreamCodec, ZlinkStreamHeaderFlags, string, ZlinkStreamMetadata, ZlinkStreamHeader> createHeader,
        Func<Message, bool> write,
        string errorMessage)
    {
        if (Interlocked.Exchange(ref _executed, 1) != 0)
        {
            throw new InvalidOperationException("Stream send builders can be executed only once.");
        }

        ReadOnlyMemory<byte> payload = ZLinkEnvelopeCodec.EncodeJsonBytes(message);
        var flags = ZlinkStreamHeaderFlags.None;

        if (_compress)
        {
            payload = ZLinkStreamProtocolDefaults.Lz4Compress(payload);
            flags |= ZlinkStreamHeaderFlags.PayloadCompressed;
        }

        var header = createHeader(ZlinkStreamCodec.Json, flags, _messageName, _metadata);
        ZLinkStreamFrameWriter.Write(write, header, payload.Span, errorMessage);
    }
}
