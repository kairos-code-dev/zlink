namespace Zlink.Framework.Contracts.Actors;

public interface IZLinkActorClient
{
    IZLinkActorSendCall SendToActor<TMessage>(
        string meshName,
        ActorRef actor,
        TMessage message);

    IZLinkActorRequestCall RequestToActor<TRequest>(
        string meshName,
        ActorRef actor,
        TRequest request);
}

public interface IZLinkActorSendCall : IZLinkMetadataCall<IZLinkActorSendCall>
{
    ValueTask Async(
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorRequestCall : IZLinkMetadataCall<IZLinkActorRequestCall>
{
    IZLinkActorRequestCall Timeout(TimeSpan timeout);

    ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default);

    ValueTask<TReply> Yield<TReply>(CancellationToken cancellationToken = default);
}
