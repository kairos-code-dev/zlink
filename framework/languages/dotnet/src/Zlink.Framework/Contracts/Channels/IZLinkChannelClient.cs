namespace Zlink.Framework.Contracts.Channels;

public interface IZLinkChannelClient
{
    IZLinkSendCall SendToChannel<TMessage>(string channelName, TMessage message);

    IZLinkRequestCall RequestToChannel<TMessage>(
        string channelName,
        TMessage request);
}