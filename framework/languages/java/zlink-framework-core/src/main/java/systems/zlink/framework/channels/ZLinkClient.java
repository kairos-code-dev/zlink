package systems.zlink.framework.channels;

public interface ZLinkClient {
    <TMessage> ZLinkSendCall sendToChannel(
        String channelName,
        TMessage message);

    <TMessage> ZLinkRequestCall requestToChannel(
        String channelName,
        TMessage message);
}
