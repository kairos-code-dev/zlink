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

namespace Systems.Zlink.Stream.Connector.Builders;

public sealed class ZlinkStreamRequestBuilder<TBody>
{
    private readonly ZlinkStreamConnector _connector;
    private readonly TBody _body;
    private string _name;
    private ZlinkStreamMetadata _metadata = ZlinkStreamMetadata.Empty;
    private TimeSpan? _timeout;
    private bool _compress;
    private int _executed;

    internal ZlinkStreamRequestBuilder(ZlinkStreamConnector connector, string name, TBody body)
    {
        _connector = connector;
        _name = name;
        _body = body;
    }

    public ZlinkStreamRequestBuilder<TBody> Name(string name)
    {
        ZlinkStreamConnector.ValidateName(name);
        _name = name;
        return this;
    }

    public ZlinkStreamRequestBuilder<TBody> Metadata(string key, string value)
    {
        _metadata = _metadata.With(key, value);
        return this;
    }

    public ZlinkStreamRequestBuilder<TBody> Metadata(ZlinkStreamMetadata metadata)
    {
        _metadata = metadata ?? throw new ArgumentNullException(nameof(metadata));
        return this;
    }

    public ZlinkStreamRequestBuilder<TBody> WithTimeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public ZlinkStreamRequestBuilder<TBody> Compress()
    {
        _compress = true;
        return this;
    }

    public async ValueTask<TReply> ExecAsync<TReply>(CancellationToken cancellationToken = default)
    {
        EnsureNotExecuted();
        return await _connector.RequestTypedAsync<TBody, TReply>(
            _name,
            _body,
            _metadata,
            _compress,
            _timeout ?? _connector.Options.RequestTimeout,
            cancellationToken).ConfigureAwait(false);
    }

    public void Exec(Action<ZlinkStreamResult> callback)
    {
        ArgumentNullException.ThrowIfNull(callback);
        EnsureNotExecuted();
        _connector.RequestTyped(_name, _body, _metadata, _compress, _timeout ?? _connector.Options.RequestTimeout, callback);
    }

    public void Exec<TReply>(Action<ZlinkStreamResult<TReply>> callback)
    {
        ArgumentNullException.ThrowIfNull(callback);
        EnsureNotExecuted();
        _connector.RequestTyped(_name, _body, _metadata, _compress, _timeout ?? _connector.Options.RequestTimeout, callback);
    }

    private void EnsureNotExecuted()
    {
        if (Interlocked.Exchange(ref _executed, 1) != 0)
        {
            throw ZlinkStreamConnector.Error(
                ZlinkStreamErrorCode.ValidationFailed,
                "Builder instances can be executed only once.");
        }
    }
}
