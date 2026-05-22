namespace Zlink.Framework.Contracts.Actors;

public interface IZLinkActorManager
{
    ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkActor?> FindAsync(
        string actorId,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkActorRemoteAddress> GetRemoteAddressAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkActor> GetOrCreateAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);
}
