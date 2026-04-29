namespace Zlink.Framework.Streams;

public interface IZLinkStream
{
    string SessionId { get; }

    RoutingId? RoutingId { get; }

    string? LocalAddr { get; }

    string? RemoteAddr { get; }

    bool Write(
        Message payload,
        SendFlags flags = SendFlags.None);

    bool Write(
        Message header,
        Message body,
        SendFlags flags = SendFlags.None);

    ValueTask CloseAsync(
        CancellationToken cancellationToken = default);
}
