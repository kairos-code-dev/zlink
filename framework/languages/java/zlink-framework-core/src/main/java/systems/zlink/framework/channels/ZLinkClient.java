package systems.zlink.framework.channels;

import systems.zlink.contracts.messaging.Message;

public interface ZLinkClient {
    ZLinkSendCall sendToChannel(
        String channelName,
        Message message);

    ZLinkRequestCall requestToChannel(
        String channelName,
        Message message);
}
