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

namespace Systems.Zlink.Stream.Connector.Transport;

internal sealed class StreamConnection(TcpClient tcpClient, System.IO.Stream stream) : IZlinkStreamConnection
{
    public bool CanWriteSegments => true;

    public async ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken)
        => await stream.ReadAsync(buffer, cancellationToken).ConfigureAwait(false);

    public async ValueTask WriteAsync(ReadOnlyMemory<byte> buffer, CancellationToken cancellationToken)
        => await stream.WriteAsync(buffer, cancellationToken).ConfigureAwait(false);

    public async ValueTask CloseAsync(CancellationToken cancellationToken)
    {
        await stream.DisposeAsync().ConfigureAwait(false);
        tcpClient.Dispose();
    }
}
