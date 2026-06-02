package systems.zlink.framework.runtime;

public interface ZLinkBackendSubscriberSocket extends ZLinkBackendConnectableSocket {
    void attachDiscovery(ZLinkBackendDiscovery discovery);

    void setSubscription(String topic);

    ZLinkBackendTopicMessage subscribe(ZLinkBackendRecvMode mode);
}
