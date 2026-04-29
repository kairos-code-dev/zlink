using Systems.Zlink.Stream.Connector.Protocol;
using Systems.Zlink.Stream.Connector.Protocol.Compression;
using Systems.Zlink.Stream.Connector.Contracts;
using System.Buffers.Binary;
using System.Text.Json;

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
    private static readonly IZlinkStreamMessageNameResolver MessageNameResolver = new ZlinkStreamMessageNameResolver();
    private static readonly IZlinkStreamCompressionCodec CompressionCodec = new ZlinkStreamLz4CompressionCodec();
    private static readonly ZlinkStreamHeaderCodec HeaderCodec = new();
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    private string _messageName = MessageNameResolver.Resolve(typeof(TMessage));
    private ZlinkStreamMetadata _metadata = ZlinkStreamMetadata.Empty;
    private bool _compress;
    private int _executed;

    public ZLinkActorStreamCallBase<TMessage> WithMetadata(string key, string value)
    {
        _metadata = _metadata.With(key, value);
        return this;
    }

    public ZLinkActorStreamCallBase<TMessage> WithPacketName(string messageName)
    {
        if (string.IsNullOrWhiteSpace(messageName))
        {
            throw new InvalidOperationException("Stream message name must not be empty.");
        }

        _messageName = messageName;
        return this;
    }

    public ZLinkActorStreamCallBase<TMessage> Compress()
    {
        _compress = true;
        return this;
    }

    public ValueTask SendAsync(CancellationToken cancellationToken = default)
    {
        _ = cancellationToken;

        if (Interlocked.Exchange(ref _executed, 1) != 0)
        {
            throw new InvalidOperationException("Stream send builders can be executed only once.");
        }

        var stream = state.Stream
            ?? throw new InvalidOperationException("Actor does not have an active client stream.");
        ReadOnlyMemory<byte> body = JsonSerializer.SerializeToUtf8Bytes(message, JsonOptions);
        var flags = ZlinkStreamHeaderFlags.None;

        if (_compress)
        {
            body = CompressionCodec.Compress(body);
            flags |= ZlinkStreamHeaderFlags.BodyCompressed;
        }

        var header = CreateHeader(ZlinkStreamCodec.Json, flags, _messageName, _metadata, state.CurrentDispatch);
        var frame = EncodeFrame(HeaderCodec.Encode(header).Span, body.Span);
        using var payloadMessage = Message.FromBytes(frame);

        if (!stream.Write(payloadMessage))
        {
            throw new InvalidOperationException("Client stream send failed.");
        }

        return ValueTask.CompletedTask;
    }

    protected abstract ZlinkStreamHeader CreateHeader(
        ZlinkStreamCodec codec,
        ZlinkStreamHeaderFlags flags,
        string messageName,
        ZlinkStreamMetadata metadata,
        ZLinkActorDispatchState? currentDispatch);

    private static byte[] EncodeFrame(ReadOnlySpan<byte> header, ReadOnlySpan<byte> body)
    {
        var frame = new byte[6 + header.Length + body.Length];
        BinaryPrimitives.WriteUInt16BigEndian(frame.AsSpan(0, 2), (ushort)header.Length);
        BinaryPrimitives.WriteUInt32BigEndian(frame.AsSpan(2, 4), (uint)body.Length);
        header.CopyTo(frame.AsSpan(6, header.Length));
        body.CopyTo(frame.AsSpan(6 + header.Length, body.Length));
        return frame;
    }
}

internal sealed class ZLinkActorSendCall<TMessage>(
    ZLinkActorRuntimeState state,
    TMessage message)
    : ZLinkActorStreamCallBase<TMessage>(state, message), IZLinkActorSendCall
{
    IZLinkActorSendCall IZLinkActorSendCall.WithMetadata(string key, string value)
        => (IZLinkActorSendCall)WithMetadata(key, value);

    IZLinkActorSendCall IZLinkActorSendCall.WithPacketName(string messageName)
        => (IZLinkActorSendCall)WithPacketName(messageName);

    IZLinkActorSendCall IZLinkActorSendCall.Compress()
        => (IZLinkActorSendCall)Compress();

    public ValueTask Async(CancellationToken cancellationToken = default)
    {
        return SendAsync(cancellationToken);
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
    IZLinkActorReplyCall IZLinkActorReplyCall.WithMetadata(string key, string value)
        => (IZLinkActorReplyCall)WithMetadata(key, value);

    IZLinkActorReplyCall IZLinkActorReplyCall.Compress()
        => (IZLinkActorReplyCall)Compress();

    public ValueTask Async(CancellationToken cancellationToken = default)
    {
        return SendAsync(cancellationToken);
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
