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

namespace Systems.Zlink.Stream.Connector.Compression;

public sealed class ZlinkStreamLz4CompressionCodec : IZlinkStreamCompressionCodec
{
    public ZlinkStreamCompression Compression => ZlinkStreamCompression.Lz4;

    public ReadOnlyMemory<byte> Compress(ReadOnlyMemory<byte> body)
        => LZ4Pickler.Pickle(body.ToArray());

    public ReadOnlyMemory<byte> Decompress(ReadOnlyMemory<byte> body)
        => LZ4Pickler.Unpickle(body.ToArray());
}
