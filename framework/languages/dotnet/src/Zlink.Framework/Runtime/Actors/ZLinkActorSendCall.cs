using Systems.Zlink.Stream.Connector.Contracts;

namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorSendCall<TMessage>(
    ZLinkActorRuntimeState state,
    TMessage message) : IZLinkActorSendCall
{
    private static readonly IZlinkStreamPacketNameResolver MessageNameResolver = ZLinkStreamProtocolDefaults.PacketNameResolver;
    private string _messageName = MessageNameResolver.Resolve(typeof(TMessage));
    private ZlinkStreamMetadata _metadata = ZlinkStreamMetadata.Empty;
    private bool _compress;
    private int _executed;

    public IZLinkActorSendCall Metadata(string key, string value)
    {
        _metadata = _metadata.With(key, value);
        return this;
    }

    public IZLinkActorSendCall PacketName(string messageName)
    {
        if (string.IsNullOrWhiteSpace(messageName))
        {
            throw new InvalidOperationException("Stream packet name must not be empty.");
        }

        _messageName = messageName;
        return this;
    }

    public IZLinkActorSendCall Compress()
    {
        _compress = true;
        return this;
    }

    public ValueTask Submit(CancellationToken cancellationToken = default)
    {
        _ = cancellationToken;

        if (Interlocked.Exchange(ref _executed, 1) != 0)
        {
            throw new InvalidOperationException("Stream send builders can be executed only once.");
        }

        var stream = state.Stream
            ?? throw new InvalidOperationException("Actor does not have an active client stream.");
        ReadOnlyMemory<byte> payload = ZLinkEnvelopeCodec.EncodeJsonBytes(message);
        var flags = ZlinkStreamHeaderFlags.None;

        if (_compress)
        {
            payload = ZLinkStreamProtocolDefaults.Lz4Compress(payload);
            flags |= ZlinkStreamHeaderFlags.PayloadCompressed;
        }

        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Json,
            flags,
            null,
            _messageName,
            _metadata);
        ZLinkStreamFrameWriter.Write(stream, header, payload, "Client stream send failed.");

        return ValueTask.CompletedTask;
    }
}
