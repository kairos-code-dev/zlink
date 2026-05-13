using System.Buffers.Binary;
using System.Collections.Concurrent;

using System.Net.Security;

using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Authentication;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
using System.Threading.Channels;

using K4os.Compression.LZ4;

namespace Systems.Zlink.Stream.Connector.Calls;

public sealed class ZlinkStreamRequestBuilder
{
    private readonly ZlinkStreamConnector _connector;
    private readonly ZlinkStreamEncodedBody _body;
    private readonly ZlinkStreamCallBuilderState _state;

    internal ZlinkStreamRequestBuilder(ZlinkStreamConnector connector, string? name, ZlinkStreamEncodedBody body)
    {
        _connector = connector;
        _body = body;
        _state = new ZlinkStreamCallBuilderState(name);
    }

    public ZlinkStreamRequestBuilder WithPacketName(string name)
    {
        _state.SetMessageName(name);
        return this;
    }

    public ZlinkStreamRequestBuilder Metadata(string key, string value)
    {
        _state.AddMetadata(key, value);
        return this;
    }

    public ZlinkStreamRequestBuilder Metadata(ZlinkStreamMetadata metadata)
    {
        _state.SetMetadata(metadata);
        return this;
    }

    public ZlinkStreamRequestBuilder WithTimeout(TimeSpan timeout)
    {
        _state.SetTimeout(timeout);
        return this;
    }

    public ZlinkStreamRequestBuilder Compress()
    {
        _state.EnableCompression();
        return this;
    }

    public async ValueTask<ZlinkStreamEncodedBody> Submit(CancellationToken cancellationToken = default)
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
