namespace Zlink.Framework.Actors;

public interface IZLinkActorStreamClient
{
    IZLinkActorSendCall Send<TMessage>(TMessage message);

    IZLinkActorReplyCall Reply<TMessage>(TMessage message);
}

public interface IZLinkActorSendCall
{
    IZLinkActorSendCall WithMetadata(string key, string value);

    IZLinkActorSendCall WithMessageName(string messageName);

    IZLinkActorSendCall Compress();

    ValueTask Async(CancellationToken cancellationToken = default);
}

public interface IZLinkActorReplyCall
{
    IZLinkActorReplyCall WithMetadata(string key, string value);

    IZLinkActorReplyCall Compress();

    ValueTask Async(CancellationToken cancellationToken = default);
}
