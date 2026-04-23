namespace Zlink.Framework;

public interface IZLinkRawStreamSession
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

    ValueTask OnRawAsync(
        IZLinkStream stream,
        global::Zlink.Message payload,
        CancellationToken cancellationToken);
}
