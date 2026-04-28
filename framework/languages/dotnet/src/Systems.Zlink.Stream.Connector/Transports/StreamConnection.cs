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

namespace Systems.Zlink.Stream.Connector.Transports;

internal sealed class StreamConnection : IZlinkStreamConnection
{
    private readonly TcpClient _tcpClient;
    private readonly global::System.IO.Stream _stream;

    public StreamConnection(TcpClient tcpClient, global::System.IO.Stream stream)
    {
        _tcpClient = tcpClient;
        _stream = stream;
    }

    public bool CanWriteSegments => true;

    public async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken)
        => await _stream.ReadAsync(buffer, cancellationToken).ConfigureAwait(false);

    public async ValueTask WriteAsync(ReadOnlyMemory<byte> buffer, CancellationToken cancellationToken)
        => await _stream.WriteAsync(buffer, cancellationToken).ConfigureAwait(false);

    public async ValueTask CloseAsync(CancellationToken cancellationToken)
    {
        await _stream.DisposeAsync().ConfigureAwait(false);
        _tcpClient.Dispose();
    }
}
