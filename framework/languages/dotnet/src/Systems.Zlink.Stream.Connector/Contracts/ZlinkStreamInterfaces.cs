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

namespace Systems.Zlink.Stream.Connector.Contracts;

public interface IZlinkStreamHeaderCodec
{
    ReadOnlyMemory<byte> Encode(ZlinkStreamHeader header);

    ZlinkStreamHeader Decode(ReadOnlyMemory<byte> header);
}

public interface IZlinkStreamPacketNameResolver
{
    string Resolve(Type bodyType);
}

public interface IZlinkStreamCompressionCodec
{
    ZlinkStreamCompression Compression { get; }

    ReadOnlyMemory<byte> Compress(ReadOnlyMemory<byte> body);

    ReadOnlyMemory<byte> Decompress(ReadOnlyMemory<byte> body);
}
