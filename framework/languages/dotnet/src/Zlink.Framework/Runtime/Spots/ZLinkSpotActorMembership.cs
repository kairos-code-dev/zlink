namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActorMembership
{
    private readonly Dictionary<string, IZLinkActor> _actorsById = new(StringComparer.Ordinal);

    public void Add(string spotName, IZLinkActor actor)
    {
        if (_actorsById.TryGetValue(actor.ActorId, out var existing)
            && !ReferenceEquals(existing, actor))
        {
            throw new InvalidOperationException(
                $"SPOT '{spotName}' already has an actor with id '{actor.ActorId}'.");
        }

        _actorsById[actor.ActorId] = actor;
    }

    public void RemoveIfCurrent(IZLinkActor actor)
    {
        if (_actorsById.TryGetValue(actor.ActorId, out var existing)
            && ReferenceEquals(existing, actor))
        {
            _actorsById.Remove(actor.ActorId);
        }
    }
}
