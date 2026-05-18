namespace Zlink.Framework.Contracts.Actors;

public interface IZLinkActor
{
    string ActorId { get; }

    IZLinkActorContext Context { get; }

    void Configure()
    {
    }

    ValueTask OnDisconnectedAsync(
        CancellationToken cancellationToken);
}
