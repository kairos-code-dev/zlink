namespace Zlink.Framework.Channels;

public interface IZLinkSendCall
{
    IZLinkSendCall WithPacketName(string messageName);

    ValueTask Async(CancellationToken cancellationToken = default);
}

public interface IZLinkRequestCall
{
    IZLinkRequestCall WithPacketName(string messageName);

    IZLinkRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default);
}

public interface IZLinkPublishCall
{
    IZLinkPublishCall WithPacketName(string messageName);

    ValueTask Async(CancellationToken cancellationToken = default);
}
