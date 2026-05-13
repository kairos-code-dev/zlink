namespace Zlink.Framework.Actors;

public interface IZLinkActorStreamClient
{
    IZLinkActorSendCall Send<TMessage>(TMessage message);

    IZLinkActorReplyCall Reply<TMessage>(TMessage message);
}

public interface IZLinkActorSendCall
{
    IZLinkActorSendCall WithMetadata(string key, string value);

    IZLinkActorSendCall WithPacketName(string messageName);

    IZLinkActorSendCall Compress();

    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkActorReplyCall
{
    IZLinkActorReplyCall WithMetadata(string key, string value);

    IZLinkActorReplyCall Compress();

    ValueTask Submit(CancellationToken cancellationToken = default);
}
