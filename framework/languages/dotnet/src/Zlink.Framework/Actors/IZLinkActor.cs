namespace Zlink.Framework.Actors;

public interface IZLinkActor
{
    string ActorKey { get; }

    ValueTask OnAttachedAsync(
        ZLinkSpot spot,
        CancellationToken cancellationToken);

    ValueTask OnDetachedAsync(
        ZLinkSpot spot,
        CancellationToken cancellationToken);

    ValueTask OnDisconnectedAsync(
        CancellationToken cancellationToken);

    ValueTask OnDispatchAsync(
        IZLinkActorContext context,
        ZlinkStreamHeader header,
        global::Zlink.Message body,
        CancellationToken cancellationToken);
}
