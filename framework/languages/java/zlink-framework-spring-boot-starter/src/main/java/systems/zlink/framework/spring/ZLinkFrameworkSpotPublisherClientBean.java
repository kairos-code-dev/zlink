package systems.zlink.framework.spring;

import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;

final class ZLinkFrameworkSpotPublisherClientBean implements ZLinkSpotPublisherClient {
    private final ZLinkFrameworkLifecycle lifecycle;

    ZLinkFrameworkSpotPublisherClientBean(ZLinkFrameworkLifecycle lifecycle) {
        this.lifecycle = lifecycle;
    }

    @Override
    public <TEvent> ZLinkPublishCall publishSpot(
        String channelName,
        String topic,
        TEvent message) {
        return lifecycle.spotPublisherClient().publishSpot(channelName, topic, message);
    }
}
