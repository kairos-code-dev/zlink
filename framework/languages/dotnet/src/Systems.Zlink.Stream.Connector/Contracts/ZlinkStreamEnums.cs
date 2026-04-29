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

public enum ZlinkStreamTransport
{
    Tcp,
    Tls,
    WebSocket,
    WebSocketSecure
}

public enum ZlinkStreamCodec : byte
{
    Raw = 0,
    Json = 1,
    MessagePack = 2,
    Protobuf = 3
}

public enum ZlinkStreamCompression
{
    None,
    Lz4
}

public enum ZlinkStreamMessageKind : byte
{
    Send = 1,
    Request = 2,
    Response = 3,
    Error = 4
}

[Flags]
public enum ZlinkStreamHeaderFlags : byte
{
    None = 0,
    HasRequestSeq = 0x01,
    HasMetadata = 0x02,
    BodyCompressed = 0x04
}

public enum ZlinkStreamErrorCode
{
    Disconnected,
    ConfigurationError,
    ValidationFailed,
    RequestTimeout,
    ConnectTimeout,
    FrameDecodeFailed,
    FrameTooLarge,
    SendFailed,
    CodecNotFound,
    CompressionFailed,
    TlsValidationFailed,
    DecompressionFailed,
    UserCallbackFailed,
    RemoteError
}
