namespace Zlink.Framework;

public interface IZLinkPacketStreamSession
{
    ValueTask OnConnectedAsync(
        IZLinkStream stream,
        CancellationToken cancellationToken);

    ValueTask OnDisconnectedAsync(
        IZLinkStream stream,
        CancellationToken cancellationToken);

    ValueTask OnErrorAsync(
        IZLinkStream stream,
        ZLinkStreamError error,
        CancellationToken cancellationToken);

    ValueTask OnPacketAsync(
        IZLinkStream stream,
        global::Zlink.Message header,
        global::Zlink.Message body,
        CancellationToken cancellationToken);
}
