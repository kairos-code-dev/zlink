using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;

namespace DeliveryDispatch.Server.CustomerGateway;

internal sealed class CustomerActor(
    string actorId,
    IZLinkActorContext context) : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; } = context;

    public async ValueTask PushStatusAsync(
        DeliveryStatusUpdatedMsg status,
        CancellationToken cancellationToken)
    {
        await Context.BoundSession
            .Send(new DeliveryStatusNotify(
                status.DeliveryId,
                status.Status,
                status.CourierId,
                status.OccurredAt))
            .SubmitAsync(cancellationToken);
    }
}

internal sealed class CustomerActorFactory : IZLinkActorFactory
{
    public ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default)
    {
        return ValueTask.FromResult<IZLinkActor>(new CustomerActor(actorId, context));
    }
}
