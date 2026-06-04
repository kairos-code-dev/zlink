package systems.zlink.framework.runtime.backend;

public interface ZLinkBackendSubscriberSocket extends ZLinkBackendConnectableSocket {
    void attachDiscovery(ZLinkBackendDiscovery discovery);

    void setChannelName(String channelName);

    void setSubscription(String topic);

    ZLinkBackendTopicMessage subscribe(ZLinkBackendRecvMode mode);
}
