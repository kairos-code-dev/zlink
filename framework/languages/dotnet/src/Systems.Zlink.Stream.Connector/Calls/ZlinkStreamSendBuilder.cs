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

public sealed class ZlinkStreamSendBuilder
{
    private readonly ZlinkStreamConnector _connector;
    private readonly ZlinkStreamEncodedBody _body;
    private readonly ZlinkStreamCallBuilderState _state;

    internal ZlinkStreamSendBuilder(ZlinkStreamConnector connector, string? name, ZlinkStreamEncodedBody body)
    {
        _connector = connector;
        _body = body;
        _state = new ZlinkStreamCallBuilderState(name);
    }

    public ZlinkStreamSendBuilder WithMessageName(string name)
    {
        _state.SetMessageName(name);
        return this;
    }

    public ZlinkStreamSendBuilder Metadata(string key, string value)
    {
        _state.AddMetadata(key, value);
        return this;
    }

    public ZlinkStreamSendBuilder Metadata(ZlinkStreamMetadata metadata)
    {
        _state.SetMetadata(metadata);
        return this;
    }

    public ZlinkStreamSendBuilder WithTimeout(TimeSpan timeout)
    {
        _state.SetTimeout(timeout);
        return this;
    }

    public ZlinkStreamSendBuilder Compress()
    {
        _state.EnableCompression();
        return this;
    }

    public void Exec(CancellationToken cancellationToken = default)
    {
        _state.EnsureNotExecuted();
        _connector.SendEncoded(
            ZlinkStreamMessageKind.Send,
            _state.ResolveMessageName(),
            _body,
            _state.Metadata,
            _state.Compress,
            _state.Timeout ?? _connector.Options.SendTimeout,
            cancellationToken);
    }

    public async ValueTask ExecAsync(CancellationToken cancellationToken = default)
    {
        _state.EnsureNotExecuted();
        await _connector.SendEncodedAsync(
            ZlinkStreamMessageKind.Send,
            _state.ResolveMessageName(),
            _body,
            _state.Metadata,
            _state.Compress,
            _state.Timeout ?? _connector.Options.SendTimeout,
            cancellationToken).ConfigureAwait(false);
    }
}
