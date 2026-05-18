namespace Systems.Zlink.Stream.Connector.Contracts;

public readonly record struct ZlinkStreamRequestSeq(ulong Value);

public sealed record ZlinkStreamEncodedPayload(
    ZlinkStreamCodec Codec,
    ReadOnlyMemory<byte> Payload,
    Type? MessageType = null);

public sealed record ZlinkStreamMessage(
    string Name,
    ZlinkStreamMetadata Metadata,
    object? Payload);

public sealed record ZlinkStreamMessage<TPayload>(
    string Name,
    ZlinkStreamMetadata Metadata,
    TPayload Payload);

public sealed record ZlinkStreamHeader(
    ZlinkStreamMessageKind Kind,
    ZlinkStreamCodec Codec,
    ZlinkStreamHeaderFlags Flags,
    ZlinkStreamRequestSeq? RequestSeq,
    string Name,
    ZlinkStreamMetadata Metadata);

public sealed record ZlinkStreamError(
    ZlinkStreamErrorCode Code,
    string Message,
    Exception? Exception = null);

public sealed record ZlinkStreamConnectionStateChanged(
    ZlinkStreamConnectionState Previous,
    ZlinkStreamConnectionState Current,
    ZlinkStreamError? Error = null);

public sealed class ZlinkStreamException(ZlinkStreamError error) : Exception(error.Message, error.Exception)
{
    public ZlinkStreamError Error { get; } = error;
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
