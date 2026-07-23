namespace Zlink.Framework.Contracts.Actors;

public interface IZLinkActorFactory
{
    ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorFactory<TActor> : IZLinkActorFactory
    where TActor : class, IZLinkActor
{
    new ValueTask<TActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default);

    async ValueTask<IZLinkActor> IZLinkActorFactory.CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken)
    {
        return await CreateAsync(actorId, context, cancellationToken)
            .ConfigureAwait(false);
    }
}
