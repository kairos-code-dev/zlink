namespace Systems.Zlink.Stream.Connector.Runtime.Calls;

internal sealed class ZlinkStreamSendBuilder : IZlinkStreamSendCall
{
    private readonly ZlinkStreamEncodedPayload _body;
    private readonly IZlinkStreamConnectorInternal _connector;
    private readonly ZlinkStreamCallBuilderState _state;

    internal ZlinkStreamSendBuilder(IZlinkStreamConnectorInternal connector, string? name,
        ZlinkStreamEncodedPayload payload)
    {
        _connector = connector;
        _body = payload;
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

    public void Submit(CancellationToken cancellationToken = default)
    {
        _state.EnsureNotExecuted();
        var name = _state.ResolveMessageName();
        _connector.ValidateSendEncoded(
            ZlinkStreamMessageKind.Send,
            name,
            _body,
            _state.Metadata,
            _state.Compress);

        _ = SubmitAsync(cancellationToken).AsTask();
    }

    private async ValueTask SubmitAsync(CancellationToken cancellationToken)
    {
        await _connector.SendEncodedAsync(
            ZlinkStreamMessageKind.Send,
            _state.ResolveMessageName(),
            _body,
            _state.Metadata,
            _state.Compress,
            cancellationToken).ConfigureAwait(false);
    }
}
