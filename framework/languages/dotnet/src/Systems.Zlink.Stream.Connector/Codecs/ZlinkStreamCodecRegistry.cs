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

namespace Systems.Zlink.Stream.Connector.Codecs;

public sealed class ZlinkStreamCodecRegistry : IZlinkStreamCodecRegistry
{
    private readonly List<IZlinkStreamBodyCodec> _codecs;

    public ZlinkStreamCodecRegistry(ZlinkStreamCodec defaultCodec)
        : this(defaultCodec, new IZlinkStreamBodyCodec[] { new ZlinkStreamRawBodyCodec(), new ZlinkStreamJsonBodyCodec() })
    {
    }

    public ZlinkStreamCodecRegistry(
        ZlinkStreamCodec defaultCodec,
        IEnumerable<IZlinkStreamBodyCodec> codecs)
    {
        DefaultCodec = defaultCodec;
        _codecs = codecs.ToList();
    }

    public ZlinkStreamCodec DefaultCodec { get; }

    public IZlinkStreamBodyCodec ResolveForSend(Type bodyType)
        => _codecs.FirstOrDefault(codec => codec.Codec != DefaultCodec && codec.CanSerialize(bodyType))
            ?? _codecs.FirstOrDefault(codec => codec.Codec == DefaultCodec && codec.CanSerialize(bodyType))
            ?? throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.CodecNotFound, $"No stream codec can serialize {bodyType.FullName}.");

    public IZlinkStreamBodyCodec ResolveForReceive(Type bodyType, ZlinkStreamCodec codec)
        => _codecs.FirstOrDefault(candidate => candidate.Codec == codec)
            ?? throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.CodecNotFound, $"No stream codec is registered for {codec}.");
}
