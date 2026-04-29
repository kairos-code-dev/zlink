namespace Zlink.Framework.Channels;

public interface IZLinkClient
{
    IZLinkSendCall Send<TMessage>(string channelName, TMessage message);

    IZLinkRequestCall Request<TMessage>(
        string channelName,
        TMessage request);
}
