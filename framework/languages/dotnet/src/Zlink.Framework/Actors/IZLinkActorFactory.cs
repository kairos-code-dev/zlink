namespace Zlink.Framework.Actors;

public interface IZLinkActorFactory
{
    ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        CancellationToken cancellationToken = default);
}
