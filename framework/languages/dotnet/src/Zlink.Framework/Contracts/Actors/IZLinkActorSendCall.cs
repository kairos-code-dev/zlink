namespace Zlink.Framework.Contracts.Actors;

public interface IZLinkActorSendCall
{
    IZLinkActorSendCall Metadata(string key, string value);

    IZLinkActorSendCall PacketName(string messageName);

    IZLinkActorSendCall Compress();

    ValueTask Submit(CancellationToken cancellationToken = default);
}
