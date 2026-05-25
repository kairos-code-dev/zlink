namespace Zlink.Framework.Contracts.Streams;

public interface IZLinkActorRef
{
    string ActorId { get; }

    string ActorType { get; }

    ActorRef Actor { get; }

    bool IsRemote { get; }

    ZLinkActorRemoteAddress RemoteAddress { get; }
}
