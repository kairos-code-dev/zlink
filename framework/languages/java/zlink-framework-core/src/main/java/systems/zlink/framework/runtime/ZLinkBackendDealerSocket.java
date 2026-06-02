package systems.zlink.framework.runtime;

import java.time.Duration;
import java.util.List;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;

public interface ZLinkBackendDealerSocket extends ZLinkBackendConnectableSocket {
    void attachDiscovery(ZLinkBackendDiscovery discovery);

    boolean send(List<Message> parts, SendFlags flags);

    boolean request(List<Message> parts, ZLinkBackendRequestCallback callback, SendFlags flags, Duration timeout);

    ZLinkBackendReceived recv(ZLinkBackendRecvMode mode);
}
