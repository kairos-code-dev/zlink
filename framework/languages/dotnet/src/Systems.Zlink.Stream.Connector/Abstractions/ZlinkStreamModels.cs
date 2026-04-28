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

namespace Systems.Zlink.Stream.Connector.Abstractions;

public readonly record struct ZlinkStreamRequestId(ulong Value);

public sealed record ZlinkStreamPacket(
    ReadOnlyMemory<byte> Header,
    ReadOnlyMemory<byte> Body);

public sealed record ZlinkStreamMessage(
    string Name,
    ZlinkStreamMetadata Metadata,
    object? Body);

public sealed record ZlinkStreamMessage<TBody>(
    string Name,
    ZlinkStreamMetadata Metadata,
    TBody Body);

public sealed record ZlinkStreamHeader(
    ZlinkStreamMessageKind Kind,
    ZlinkStreamCodec Codec,
    ZlinkStreamHeaderFlags Flags,
    ZlinkStreamRequestId? RequestId,
    string Name,
    ZlinkStreamMetadata Metadata);

public sealed record ZlinkStreamError(
    ZlinkStreamErrorCode Code,
    string Message,
    Exception? Exception = null);

public sealed class ZlinkStreamException : Exception
{
    public ZlinkStreamException(ZlinkStreamError error)
        : base(error.Message, error.Exception)
    {
        Error = error;
    }

    public ZlinkStreamError Error { get; }
}

public readonly struct ZlinkStreamResult
{
    private ZlinkStreamResult(ZlinkStreamError? error)
    {
        Error = error;
    }

    public bool IsSuccess => Error is null;

    public ZlinkStreamError? Error { get; }

    public static ZlinkStreamResult Success() => new(null);

    public static ZlinkStreamResult Failure(ZlinkStreamError error) => new(error);
}

public readonly struct ZlinkStreamResult<T>
{
    private ZlinkStreamResult(T? value, ZlinkStreamError? error)
    {
        Value = value;
        Error = error;
    }

    public bool IsSuccess => Error is null;

    public T? Value { get; }

    public ZlinkStreamError? Error { get; }

    public static ZlinkStreamResult<T> Success(T value) => new(value, null);

    public static ZlinkStreamResult<T> Failure(ZlinkStreamError error) => new(default, error);
}
