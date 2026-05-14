namespace Zlink.Framework.Contracts.Channels;

public interface IZLinkSendCall
{
    IZLinkSendCall PacketName(string messageName);

    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkRequestCall
{
    IZLinkRequestCall PacketName(string messageName);

    IZLinkRequestCall Timeout(TimeSpan timeout);

    ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default);
}

public interface IZLinkPublishCall
{
    IZLinkPublishCall PacketName(string messageName);

    ValueTask Submit(CancellationToken cancellationToken = default);
}
