using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;

namespace DeliveryDispatch.Server.Tracking;

internal sealed class CustomerActor(
    string actorId,
    IZLinkActorContext context) : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; } = context;
}

internal sealed class CustomerActorFactory : IZLinkActorFactory
{
    public ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult<IZLinkActor>(new CustomerActor(actorId, context));
    }
}
