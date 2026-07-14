namespace Zlink.Framework.Contracts.Actors;

public interface IZLinkActorClient
{
    IZLinkActorSendCall SendToActor<TMessage>(
        ActorRef actor,
        TMessage message);

    IZLinkActorRequestCall RequestToActor<TRequest>(
        ActorRef actor,
        TRequest request);
}

public interface IZLinkActorSendCall
{
    void Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkActorRequestCall
{
    IZLinkActorRequestCall Timeout(TimeSpan timeout);

    ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default);

    ValueTask<TReply> Yield<TReply>(CancellationToken cancellationToken = default);
}
