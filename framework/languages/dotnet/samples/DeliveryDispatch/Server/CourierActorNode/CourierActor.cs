using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;

namespace DeliveryDispatch.Server.CourierActorNode;

internal sealed class CourierActor(
    string actorId,
    IZLinkActorContext context) : IZLinkActor
{
    private readonly object _gate = new();
    private readonly Dictionary<string, TaskCompletionSource<CourierDecisionMsg>> _pending = new(StringComparer.Ordinal);

    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; } = context;

    public async ValueTask<OfferDeliveryRes> OfferAsync(
        OfferDeliveryReq offer,
        CancellationToken cancellationToken)
    {
        var pending = new TaskCompletionSource<CourierDecisionMsg>(TaskCreationOptions.RunContinuationsAsynchronously);
        lock (_gate)
        {
            _pending[offer.DeliveryId] = pending;
        }

        try
        {
            Context.BoundSession.Send(new OfferDeliveryNotify(
                    offer.CourierId,
                    offer.DeliveryId,
                    offer.PickupAddress,
                    offer.DropoffAddress))
                .Submit(cancellationToken);
            var decision = await pending.Task.WaitAsync(cancellationToken);
            return new OfferDeliveryRes(
                offer.DeliveryId,
                ActorId,
                decision.Accepted,
                decision.Reason);
        }
        finally
        {
            lock (_gate)
            {
                _pending.Remove(offer.DeliveryId);
            }
        }
    }

    public void Complete(CourierDecisionMsg decision)
    {
        TaskCompletionSource<CourierDecisionMsg>? pending;
        lock (_gate)
        {
            _pending.TryGetValue(decision.DeliveryId, out pending);
        }

        pending?.TrySetResult(decision);
    }
}

internal sealed class CourierActorFactory : IZLinkActorFactory
{
    public ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default)
    {
        return ValueTask.FromResult<IZLinkActor>(new CourierActor(actorId, context));
    }
}
