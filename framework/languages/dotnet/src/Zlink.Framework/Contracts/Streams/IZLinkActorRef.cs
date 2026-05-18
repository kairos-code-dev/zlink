namespace Zlink.Framework.Contracts.Streams;

public interface IZLinkActorRef
{
    string ActorId { get; }

    string ActorType { get; }

    ValueTask NotifyDisconnectedAsync(
        CancellationToken cancellationToken = default);
}
