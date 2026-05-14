using Systems.Zlink.Stream.Connector.Contracts;

namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorStreamClient(
    ZLinkActorRuntimeState state) : IZLinkActorStreamClient
{
    public IZLinkActorSendCall Send<TMessage>(TMessage message)
    {
        return new ZLinkActorSendCall<TMessage>(state, message);
    }

    public IZLinkActorReplyCall Reply<TMessage>(TMessage message)
    {
        return new ZLinkActorReplyCall<TMessage>(state, message);
    }
}

internal abstract class ZLinkActorStreamCallBase<TMessage>(
    ZLinkActorRuntimeState state,
    TMessage message)
{
    private static readonly IZlinkStreamPacketNameResolver MessageNameResolver = ZlinkStreamDefaultCodecs.PacketNameResolver();
    private static readonly IZlinkStreamCompressionCodec CompressionCodec = ZlinkStreamDefaultCodecs.Lz4Compression();
    private string _messageName = MessageNameResolver.Resolve(typeof(TMessage));
    private ZlinkStreamMetadata _metadata = ZlinkStreamMetadata.Empty;
    private bool _compress;
    private int _executed;

    public ZLinkActorStreamCallBase<TMessage> Metadata(string key, string value)
    {
        _metadata = _metadata.With(key, value);
        return this;
    }

    public ZLinkActorStreamCallBase<TMessage> PacketName(string messageName)
    {
        if (string.IsNullOrWhiteSpace(messageName))
        {
            throw new InvalidOperationException("Stream packet name must not be empty.");
        }

        _messageName = messageName;
        return this;
    }

    public ZLinkActorStreamCallBase<TMessage> Compress()
    {
        _compress = true;
        return this;
    }

    protected ValueTask ExecuteAsync(CancellationToken cancellationToken = default)
    {
        _ = cancellationToken;

        if (Interlocked.Exchange(ref _executed, 1) != 0)
        {
            throw new InvalidOperationException("Stream send builders can be executed only once.");
        }

        var stream = state.Stream
            ?? throw new InvalidOperationException("Actor does not have an active client stream.");
        ReadOnlyMemory<byte> body = ZLinkEnvelopeCodec.EncodeJsonBytes(message);
        var flags = ZlinkStreamHeaderFlags.None;

        if (_compress)
        {
            body = CompressionCodec.Compress(body);
            flags |= ZlinkStreamHeaderFlags.BodyCompressed;
        }

        var header = CreateHeader(ZlinkStreamCodec.Json, flags, _messageName, _metadata, state.CurrentDispatch);
        ZLinkStreamFrameWriter.Write(stream, header, body, "Client stream send failed.");

        return ValueTask.CompletedTask;
    }

    protected abstract ZlinkStreamHeader CreateHeader(
        ZlinkStreamCodec codec,
        ZlinkStreamHeaderFlags flags,
        string messageName,
        ZlinkStreamMetadata metadata,
        ZLinkActorDispatchState? currentDispatch);

}

internal sealed class ZLinkActorSendCall<TMessage>(
    ZLinkActorRuntimeState state,
    TMessage message)
    : ZLinkActorStreamCallBase<TMessage>(state, message), IZLinkActorSendCall
{
    IZLinkActorSendCall IZLinkActorSendCall.Metadata(string key, string value)
        => (IZLinkActorSendCall)Metadata(key, value);

    IZLinkActorSendCall IZLinkActorSendCall.PacketName(string messageName)
        => (IZLinkActorSendCall)PacketName(messageName);

    IZLinkActorSendCall IZLinkActorSendCall.Compress()
        => (IZLinkActorSendCall)Compress();

    public ValueTask Submit(CancellationToken cancellationToken = default)
    {
        return ExecuteAsync(cancellationToken);
    }

    protected override ZlinkStreamHeader CreateHeader(
        ZlinkStreamCodec codec,
        ZlinkStreamHeaderFlags flags,
        string messageName,
        ZlinkStreamMetadata metadata,
        ZLinkActorDispatchState? currentDispatch)
    {
        _ = currentDispatch;
        return new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            codec,
            flags,
            null,
            messageName,
            metadata);
    }
}

internal sealed class ZLinkActorReplyCall<TMessage>(
    ZLinkActorRuntimeState state,
    TMessage message)
    : ZLinkActorStreamCallBase<TMessage>(state, message), IZLinkActorReplyCall
{
    IZLinkActorReplyCall IZLinkActorReplyCall.Metadata(string key, string value)
        => (IZLinkActorReplyCall)Metadata(key, value);

    IZLinkActorReplyCall IZLinkActorReplyCall.Compress()
        => (IZLinkActorReplyCall)Compress();

    public ValueTask Submit(CancellationToken cancellationToken = default)
    {
        return ExecuteAsync(cancellationToken);
    }

    protected override ZlinkStreamHeader CreateHeader(
        ZlinkStreamCodec codec,
        ZlinkStreamHeaderFlags flags,
        string messageName,
        ZlinkStreamMetadata metadata,
        ZLinkActorDispatchState? currentDispatch)
    {
        if (currentDispatch?.Header.RequestSeq is not { } requestSeq)
        {
            throw new InvalidOperationException("Reply is only available while handling a request packet.");
        }

        return new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Response,
            codec,
            flags | ZlinkStreamHeaderFlags.HasRequestSeq,
            requestSeq,
            messageName,
            metadata);
    }
}
