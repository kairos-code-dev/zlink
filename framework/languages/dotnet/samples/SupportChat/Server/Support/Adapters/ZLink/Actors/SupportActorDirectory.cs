namespace SupportChat.Server.Support.Adapters.ZLink.Actors;

internal sealed class SupportActorDirectory
{
    private readonly Dictionary<string, SupportUserActor> _actors = new(StringComparer.Ordinal);

    public void AddOrUpdate(SupportUserActor actor)
    {
        _actors[actor.ActorId] = actor;
    }

    public SupportUserActor Get(string actorId)
    {
        return _actors.TryGetValue(actorId, out var actor)
            ? actor
            : throw new InvalidOperationException($"Support actor is not available. actor={actorId}");
    }
}
