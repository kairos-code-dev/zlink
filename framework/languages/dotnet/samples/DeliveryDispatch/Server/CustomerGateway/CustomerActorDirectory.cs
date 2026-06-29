using DeliveryDispatch.Shared.Contracts;

namespace DeliveryDispatch.Server.CustomerGateway;

internal sealed class CustomerActorDirectory
{
    private readonly object _gate = new();
    private readonly Dictionary<string, CustomerActor> _actors = new(StringComparer.Ordinal);
    private readonly Dictionary<string, string> _deliveryCustomers = new(StringComparer.Ordinal);

    public void Register(CustomerActor actor)
    {
        lock (_gate)
        {
            _actors[actor.ActorId] = actor;
        }
    }

    public void Subscribe(string customerId, string deliveryId)
    {
        lock (_gate)
        {
            _deliveryCustomers[deliveryId] = customerId;
        }
    }

    public async ValueTask PushAsync(
        DeliveryStatusChanged status,
        CancellationToken cancellationToken)
    {
        CustomerActor? actor = null;
        lock (_gate)
        {
            if (_deliveryCustomers.TryGetValue(status.DeliveryId, out var customerId))
            {
                _actors.TryGetValue(customerId, out actor);
            }
        }

        if (actor is null)
        {
            Console.Error.WriteLine($"deliverydispatch customer-gateway: no bound customer for delivery={status.DeliveryId}");
            return;
        }

        await actor.Context.BoundSession
            .Send(new DeliveryStatusNotify(
                status.DeliveryId,
                status.Status,
                status.CourierId,
                status.OccurredAt))
            .Async(cancellationToken);
    }
}
