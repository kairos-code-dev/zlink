namespace Zlink.Framework.Channels;

public interface IZLinkClient
{
    IZLinkSendCall Send<TMessage>(string channelName, TMessage message);

    IZLinkRequestCall<TReply> Request<TReply>(
        string channelName,
        IZLinkRequest<TReply> request);
}
