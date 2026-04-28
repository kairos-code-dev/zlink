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

    ValueTask SendAsync(CancellationToken cancellationToken = default);
}

public interface IZLinkActorReplyCall
{
    IZLinkActorReplyCall WithMetadata(string key, string value);

    IZLinkActorReplyCall WithMessageName(string messageName);

    IZLinkActorReplyCall Compress();

    ValueTask SendAsync(CancellationToken cancellationToken = default);
}
