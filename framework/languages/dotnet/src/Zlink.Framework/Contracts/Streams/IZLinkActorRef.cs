namespace Zlink.Framework.Contracts.Streams;

public interface IZLinkActorRef
{
    string ActorId { get; }

    string ActorType { get; }

    bool IsRemote { get; }

    ZLinkActorRemoteAddress RemoteAddress { get; }

    ValueTask NotifyDisconnectedAsync(
        CancellationToken cancellationToken = default);
}
