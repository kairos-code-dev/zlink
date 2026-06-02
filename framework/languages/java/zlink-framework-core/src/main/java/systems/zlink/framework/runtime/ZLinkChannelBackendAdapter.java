package systems.zlink.framework.runtime;

public interface ZLinkChannelBackendAdapter {
    ZLinkBackendContext createContext();

    ZLinkBackendDiscovery createDiscovery(
        ZLinkBackendContext context,
        ZLinkBackendAutoConnectType autoConnectType,
        String channelName);

    ZLinkBackendDealerSocket createDealerSocket(ZLinkBackendContext context);

    ZLinkBackendRouterSocket createRouterSocket(ZLinkBackendContext context);

    ZLinkBackendPublisherSocket createPublisherSocket(ZLinkBackendContext context);

    ZLinkBackendSubscriberSocket createSubscriberSocket(ZLinkBackendContext context);
}
