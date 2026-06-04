package systems.zlink.framework.runtime.backend;

import java.util.List;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;

public interface ZLinkBackendPublisherSocket extends ZLinkBackendSocket {
    void attachDiscovery(ZLinkBackendDiscovery discovery);

    void setChannelName(String channelName);

    boolean publish(String topic, List<Message> parts, SendFlags flags);
}
