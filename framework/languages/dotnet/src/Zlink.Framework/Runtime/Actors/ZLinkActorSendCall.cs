namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorSendCall<TMessage>(
    ZLinkActorRuntimeState state,
    TMessage message) : IZLinkActorSendCall
{
    private readonly ZLinkStreamSendBuilder<TMessage> _builder = new(message);

    public IZLinkActorSendCall Metadata(string key, string value)
    {
        _builder.AddMetadata(key, value);
        return this;
    }

    public IZLinkActorSendCall PacketName(string messageName)
    {
        _builder.SetPacketName(messageName);
        return this;
    }

    public IZLinkActorSendCall Compress()
    {
        _builder.EnableCompression();
        return this;
    }

    public ValueTask Submit(CancellationToken cancellationToken = default)
    {
        _ = cancellationToken;

        var stream = state.Stream
            ?? throw new InvalidOperationException("Actor does not have an active client stream.");
        _builder.Write(
            static (codec, flags, messageName, metadata) => new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Send,
                codec,
                flags,
                null,
                messageName,
                metadata),
            payload => stream.Write(payload),
            "Client stream send failed.");

        return ValueTask.CompletedTask;
    }
}
