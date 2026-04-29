namespace Zlink.Framework.Channels;

public interface IZLinkSendCall
{
    IZLinkSendCall WithMessageName(string messageName);

    IZLinkSendCall WithDontWait();

    bool Sync();
}

public interface IZLinkRequestCall
{
    IZLinkRequestCall WithMessageName(string messageName);

    IZLinkRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default);
}

public interface IZLinkPublishCall
{
    IZLinkPublishCall WithMessageName(string messageName);

    IZLinkPublishCall WithDontWait();

    bool Sync();
}
