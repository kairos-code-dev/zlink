package systems.zlink.framework.runtime.backend;

public interface ZLinkBackendSubscriberSocket extends ZLinkBackendConnectableSocket {
    void attachDiscovery(ZLinkBackendDiscovery discovery);

    void setSubscription(String topic);

    ZLinkBackendTopicMessage subscribe(ZLinkBackendRecvMode mode);
}
