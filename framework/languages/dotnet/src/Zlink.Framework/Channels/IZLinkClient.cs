namespace Zlink.Framework;

public interface IZLinkClient
{
    IZLinkSendCall Send<TMessage>(string channelName, TMessage message);

    IZLinkRequestCall<TReply> Request<TReply>(
        string channelName,
        IZLinkRequest<TReply> request);

    IZLinkSendCall SendTo<TMessage>(
        global::Zlink.RoutingId targetRid,
        global::Zlink.RoutingId spotRid,
        TMessage message);

    IZLinkRequestCall<TReply> RequestTo<TReply>(
        global::Zlink.RoutingId targetRid,
        global::Zlink.RoutingId spotRid,
        IZLinkRequest<TReply> request);
}
