using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;

namespace DeliveryDispatch.Server.CourierActorNode;

internal sealed class CourierActor(
    string actorId,
    IZLinkActorContext context) : IZLinkActor
{
    private readonly object _gate = new();
    private readonly Dictionary<string, TaskCompletionSource<CourierDecision>> _pending = new(StringComparer.Ordinal);

    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; } = context;

    public async ValueTask<OfferDeliveryResult> OfferAsync(
        OfferDelivery offer,
        CancellationToken cancellationToken)
    {
        var pending = new TaskCompletionSource<CourierDecision>(TaskCreationOptions.RunContinuationsAsynchronously);
        lock (_gate)
        {
            _pending[offer.DeliveryId] = pending;
        }

        try
        {
            await Context.BoundSession.Send(offer).Async(cancellationToken);
            var decision = await pending.Task.WaitAsync(cancellationToken);
            return new OfferDeliveryResult(
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

    public void Complete(CourierDecision decision)
    {
        TaskCompletionSource<CourierDecision>? pending;
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
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult<IZLinkActor>(new CourierActor(actorId, context));
    }
}
