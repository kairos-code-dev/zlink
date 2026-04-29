namespace Zlink.Framework.Actors;

public interface IZLinkActor
{
    string ActorId { get; }

    IZLinkActorContext Context { get; set; }

    void Configure()
    {
    }

    ValueTask OnDisconnectedAsync(
        CancellationToken cancellationToken);
}
