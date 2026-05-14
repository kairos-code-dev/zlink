using Systems.Zlink.Stream.Connector.Runtime;

namespace Systems.Zlink.Stream.Connector.Calls;

internal sealed class ZlinkStreamSendBuilder : IZlinkStreamSendCall
{
    private readonly IZlinkStreamConnectorInternal _connector;
    private readonly ZlinkStreamEncodedBody _body;
    private readonly ZlinkStreamCallBuilderState _state;

    internal ZlinkStreamSendBuilder(IZlinkStreamConnectorInternal connector, string? name, ZlinkStreamEncodedBody body)
    {
        _connector = connector;
        _body = body;
        _state = new ZlinkStreamCallBuilderState(name);
    }

    public IZlinkStreamSendCall PacketName(string name)
    {
        _state.SetMessageName(name);
        return this;
    }

    public IZlinkStreamSendCall Metadata(string key, string value)
    {
        _state.AddMetadata(key, value);
        return this;
    }

    public IZlinkStreamSendCall Metadata(ZlinkStreamMetadata metadata)
    {
        _state.SetMetadata(metadata);
        return this;
    }

    public IZlinkStreamSendCall Compress()
    {
        _state.EnableCompression();
        return this;
    }

    public async ValueTask Submit(CancellationToken cancellationToken = default)
    {
        _state.EnsureNotExecuted();
        await _connector.SendEncodedAsync(
            ZlinkStreamMessageKind.Send,
            _state.ResolveMessageName(),
            _body,
            _state.Metadata,
            _state.Compress,
            cancellationToken).ConfigureAwait(false);
    }
}
