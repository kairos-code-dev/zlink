namespace DeliveryDispatch.Server.CourierActorNode;

internal sealed record ActorDirectoryEntry(
    CourierActor Actor,
    Systems.Zlink.ActorRef Ref);

internal sealed class ActorDirectory
{
    private readonly object _gate = new();
    private readonly Dictionary<string, ActorDirectoryEntry> _actors = new(StringComparer.Ordinal);

    public void Register(CourierActor actor, Systems.Zlink.ActorRef actorRef)
    {
        lock (_gate)
        {
            _actors[actor.ActorId] = new ActorDirectoryEntry(actor, actorRef);
        }
    }

    public CourierActor Require(string courierId)
    {
        return RequireEntry(courierId).Actor;
    }

    public Systems.Zlink.ActorRef RequireRef(string courierId)
    {
        return RequireEntry(courierId).Ref;
    }

    private ActorDirectoryEntry RequireEntry(string courierId)
    {
        lock (_gate)
        {
            if (_actors.TryGetValue(courierId, out var entry))
            {
                return entry;
            }
        }

        throw new InvalidOperationException($"Courier actor is not available on this node: {courierId}");
    }
}
