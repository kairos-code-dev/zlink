namespace Zlink.Framework.Contracts.Actors;

public interface IZLinkActorStreamClient
{
    IZLinkActorSendCall Send<TMessage>(TMessage message);

    IZLinkActorReplyCall Reply<TMessage>(TMessage message);
}

public interface IZLinkActorSendCall
{
    IZLinkActorSendCall Metadata(string key, string value);

    IZLinkActorSendCall PacketName(string messageName);

    IZLinkActorSendCall Compress();

    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkActorReplyCall
{
    IZLinkActorReplyCall Metadata(string key, string value);

    IZLinkActorReplyCall Compress();

    ValueTask Submit(CancellationToken cancellationToken = default);
}
