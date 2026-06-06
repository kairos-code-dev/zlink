namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActorMembership
{
    private readonly object _gate = new();
    private readonly Dictionary<string, IZLinkActor> _actorsById = new(StringComparer.Ordinal);

    public int Count
    {
        get
        {
            lock (_gate)
            {
                return _actorsById.Count;
            }
        }
    }

    public void Add(IZLinkActor actor)
    {
        lock (_gate)
        {
            if (_actorsById.TryGetValue(actor.ActorId, out var existing)
                && !ReferenceEquals(existing, actor))
            {
                throw new InvalidOperationException(
                    $"SPOT already has an actor with id '{actor.ActorId}'.");
            }

            _actorsById[actor.ActorId] = actor;
        }
    }

    public bool TryGetActor(string actorId, out IZLinkActor? actor)
    {
        lock (_gate)
        {
            return _actorsById.TryGetValue(actorId, out actor);
        }
    }

    public void RemoveIfCurrent(IZLinkActor actor)
    {
        lock (_gate)
        {
            if (_actorsById.TryGetValue(actor.ActorId, out var existing)
                && ReferenceEquals(existing, actor))
            {
                _actorsById.Remove(actor.ActorId);
            }
        }
    }
}
