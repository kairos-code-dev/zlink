namespace Zlink.Framework.Contracts.Streams;

public interface IZLinkStream
{
    string SessionId { get; }

    RoutingId? RoutingId { get; }

    string? LocalAddr { get; }

    string? RemoteAddr { get; }

    /// <summary>
    /// Writes a raw stream frame without taking ownership of <paramref name="payload"/>.
    /// </summary>
    bool Write(
        Message payload,
        SendFlags flags = SendFlags.None);

    ValueTask CloseAsync(
        CancellationToken cancellationToken = default);
}
