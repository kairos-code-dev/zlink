namespace Zlink.Framework;

public interface IZLinkClient
{
    IZLinkSendCall Send<TMessage>(string channelName, TMessage message);

    IZLinkRequestCall<TReply> Request<TReply>(
        string channelName,
        IZLinkRequest<TReply> request);
}
