namespace Zlink.Framework.Contracts.Channels;

public interface IZLinkClientServerClient
{
    IZLinkSendCall Send<TMessage>(string channelName, TMessage message);

    IZLinkRequestCall Request<TMessage>(
        string channelName,
        TMessage request);
}

public interface IZLinkClient : IZLinkClientServerClient
{
}
