using Zlink.Framework.Contracts.Streams;

namespace Bingo.Server.Session;

internal sealed class SessionRelayState
{
    public IZLinkActorRef? Actor { get; private set; }

    public string? ActorId { get; private set; }

    public string? DisplayName { get; private set; }

    public void AttachAuthenticatedActor(
        string displayName,
        IZLinkActorRef actor)
    {
        ArgumentNullException.ThrowIfNull(actor);

        ActorId = actor.ActorId;
        DisplayName = displayName;
        Actor = actor;
    }

    public string RequireActorId(string action)
    {
        return ActorId
            ?? throw new InvalidOperationException($"Client must authenticate before {action}.");
    }

    public ValueTask<IZLinkActorRef> RequireActorAsync(
        IZLinkSessionContext context,
        string action,
        CancellationToken cancellationToken)
    {
        if (Actor is not null)
        {
            return ValueTask.FromResult(Actor);
        }

        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        throw new InvalidOperationException($"Actor handle must be attached before {action}.");
    }

    public void Clear()
    {
        Actor = null;
        ActorId = null;
        DisplayName = null;
    }
}
