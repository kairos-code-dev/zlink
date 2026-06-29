using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Systems.Zlink;

namespace DeliveryDispatch.Server.CourierGateway;

internal sealed class CourierDirectory(SampleTopology topology)
{
    private readonly object _gate = new();
    private readonly Dictionary<string, CourierBinding> _actors = new(StringComparer.Ordinal);

    public CourierSpotPlacement ChoosePlacement(string courierId)
    {
        return topology.CourierPlacement(courierId);
    }

    public CourierBinding Remember(CourierActorEnsured ensured, string sessionRoute)
    {
        var binding = new CourierBinding(ensured.CourierId, ensured.Actor, sessionRoute);
        lock (_gate)
        {
            _actors[ensured.CourierId] = binding;
        }

        return binding;
    }

    public CourierBinding Require(string courierId)
    {
        lock (_gate)
        {
            if (_actors.TryGetValue(courierId, out var binding))
            {
                return binding;
            }
        }

        throw new InvalidOperationException($"Courier actor is not bound: {courierId}");
    }
}

internal sealed record CourierBinding(
    string CourierId,
    ActorRefSnapshot Actor,
    string SessionRoute);
