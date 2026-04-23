namespace Zlink.Framework;

public interface IZLinkActor
{
    string ActorKey { get; }

    IZLinkStream? Stream { get; }

    ZLinkSpot? Spot { get; }

    ValueTask AttachAsync(
        IZLinkStream stream,
        CancellationToken cancellationToken);

    ValueTask OnAttachedAsync(
        ZLinkSpot spot,
        CancellationToken cancellationToken);

    ValueTask OnDetachedAsync(
        ZLinkSpot spot,
        CancellationToken cancellationToken);

    ValueTask OnDisconnectedAsync(
        CancellationToken cancellationToken);

    ValueTask OnDispatchAsync(
        global::Zlink.Message header,
        global::Zlink.Message body,
        CancellationToken cancellationToken);
}
