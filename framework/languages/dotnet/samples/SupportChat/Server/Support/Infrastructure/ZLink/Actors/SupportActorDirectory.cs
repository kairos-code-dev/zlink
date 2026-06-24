namespace SupportChat.Server.Support.Infrastructure.ZLink.Actors;

using Systems.Zlink;

internal sealed record SupportActorDirectoryEntry(
    ActorRef Ref,
    string DisplayName,
    string Role);

internal sealed class SupportActorDirectory
{
    private readonly Dictionary<string, SupportActorDirectoryEntry> _actors = new(StringComparer.Ordinal);

    public void AddOrUpdate(ActorRef actor, string displayName, string role)
    {
        _actors[actor.ActorId] = new SupportActorDirectoryEntry(actor, displayName, role);
    }

    public SupportActorDirectoryEntry Get(string actorId)
    {
        return _actors.TryGetValue(actorId, out var actor)
            ? actor
            : throw new InvalidOperationException($"Support actor is not available. actor={actorId}");
    }
}
