using Systems.Zlink.Stream.Connector.Runtime;

namespace Systems.Zlink.Stream.Connector.Calls;

internal sealed class ZlinkStreamRequestBuilder : IZlinkStreamRequestCall
{
    private readonly IZlinkStreamConnectorInternal _connector;
    private readonly ZlinkStreamEncodedBody _body;
    private readonly ZlinkStreamCallBuilderState _state;

    internal ZlinkStreamRequestBuilder(IZlinkStreamConnectorInternal connector, string? name, ZlinkStreamEncodedBody body)
    {
        _connector = connector;
        _body = body;
        _state = new ZlinkStreamCallBuilderState(name);
    }

    public IZlinkStreamRequestCall PacketName(string name)
    {
        _state.SetMessageName(name);
        return this;
    }

    public IZlinkStreamRequestCall Metadata(string key, string value)
    {
        _state.AddMetadata(key, value);
        return this;
    }

    public IZlinkStreamRequestCall Metadata(ZlinkStreamMetadata metadata)
    {
        _state.SetMetadata(metadata);
        return this;
    }

    public IZlinkStreamRequestCall Timeout(TimeSpan timeout)
    {
        _state.SetTimeout(timeout);
        return this;
    }

    public IZlinkStreamRequestCall Compress()
    {
        _state.EnableCompression();
        return this;
    }

    public async ValueTask<ZlinkStreamEncodedBody> SubmitAsync(CancellationToken cancellationToken = default)
    {
        _state.EnsureNotExecuted();
        return await _connector.RequestEncodedAsync(
            _state.ResolveMessageName(),
            _body,
            _state.Metadata,
            _state.Compress,
            _state.Timeout ?? _connector.Options.RequestTimeout,
            cancellationToken).ConfigureAwait(false);
    }

    public void Submit(Action<ZlinkStreamResult> callback)
    {
        ArgumentNullException.ThrowIfNull(callback);
        _state.EnsureNotExecuted();
        _connector.RequestEncoded(
            _state.ResolveMessageName(),
            _body,
            _state.Metadata,
            _state.Compress,
            _state.Timeout ?? _connector.Options.RequestTimeout,
            callback);
    }

    public void Submit(Action<ZlinkStreamResult<ZlinkStreamEncodedBody>> callback)
    {
        ArgumentNullException.ThrowIfNull(callback);
        _state.EnsureNotExecuted();
        _connector.RequestEncoded(
            _state.ResolveMessageName(),
            _body,
            _state.Metadata,
            _state.Compress,
            _state.Timeout ?? _connector.Options.RequestTimeout,
            callback);
    }
}
