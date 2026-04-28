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

public sealed class ZlinkStreamSendBuilder<TBody>
{
    private readonly ZlinkStreamConnector _connector;
    private readonly TBody _body;
    private string _name;
    private ZlinkStreamMetadata _metadata = ZlinkStreamMetadata.Empty;
    private TimeSpan? _timeout;
    private bool _compress;
    private int _executed;

    internal ZlinkStreamSendBuilder(ZlinkStreamConnector connector, string name, TBody body)
    {
        _connector = connector;
        _name = name;
        _body = body;
    }

    public ZlinkStreamSendBuilder<TBody> Name(string name)
    {
        ZlinkStreamConnector.ValidateName(name);
        _name = name;
        return this;
    }

    public ZlinkStreamSendBuilder<TBody> Metadata(string key, string value)
    {
        _metadata = _metadata.With(key, value);
        return this;
    }

    public ZlinkStreamSendBuilder<TBody> Metadata(ZlinkStreamMetadata metadata)
    {
        _metadata = metadata ?? throw new ArgumentNullException(nameof(metadata));
        return this;
    }

    public ZlinkStreamSendBuilder<TBody> WithTimeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public ZlinkStreamSendBuilder<TBody> Compress()
    {
        _compress = true;
        return this;
    }

    public void Exec(CancellationToken cancellationToken = default)
    {
        EnsureNotExecuted();
        _connector.SendTyped(
            ZlinkStreamMessageKind.Send,
            _name,
            _body,
            _metadata,
            _compress,
            _timeout ?? _connector.Options.SendTimeout,
            cancellationToken);
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
