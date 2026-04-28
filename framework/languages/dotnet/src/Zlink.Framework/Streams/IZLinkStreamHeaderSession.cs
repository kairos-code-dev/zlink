namespace Zlink.Framework.Streams;

public interface IZLinkStreamHeaderSession
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

    ValueTask OnDispatchAsync(
        IZLinkStream stream,
        ZlinkStreamHeader header,
        global::Zlink.Message body,
        CancellationToken cancellationToken);
}
