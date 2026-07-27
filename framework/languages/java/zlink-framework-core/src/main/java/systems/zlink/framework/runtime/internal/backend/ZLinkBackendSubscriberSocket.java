package systems.zlink.framework.runtime.internal.backend;

public interface ZLinkBackendSubscriberSocket extends ZLinkBackendConnectableSocket {
    void setChannelName(String channelName);

    void setSubscription(String topic);

    ZLinkBackendTopicMessage subscribe(ZLinkBackendRecvMode mode);
}
