namespace Zlink.Framework.Contracts.Channels;

public interface IZLinkChannelClient
{
    IZLinkSendCall SendChannel<TMessage>(string channelName, TMessage message);

    IZLinkRequestCall RequestChannel<TMessage>(
        string channelName,
        TMessage request);
}
