package systems.zlink.framework.spring;

import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotCreateResult;
import systems.zlink.framework.spots.ZLinkSpotInfo;
import systems.zlink.framework.spots.ZLinkSpotManager;

final class ZLinkFrameworkSpotManagerBean implements ZLinkSpotManager {
    private final ZLinkFrameworkLifecycle lifecycle;

    ZLinkFrameworkSpotManagerBean(ZLinkFrameworkLifecycle lifecycle) {
        this.lifecycle = lifecycle;
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> createAsync(
        Class<? extends ZLinkSpot> spotType) {
        return lifecycle.spotManager().createAsync(spotType);
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> createAsync(
        Class<? extends ZLinkSpot> spotType,
        List<Message> createParts) {
        return lifecycle.spotManager().createAsync(spotType, createParts);
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> createAsync(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid) {
        return lifecycle.spotManager().createAsync(spotType, spotRid);
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> getOrCreateAsync(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid) {
        return lifecycle.spotManager().getOrCreateAsync(spotType, spotRid);
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> getOrCreateAsync(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid,
        List<Message> createParts) {
        return lifecycle.spotManager().getOrCreateAsync(spotType, spotRid, createParts);
    }

    @Override
    public CompletionStage<Optional<ZLinkSpotInfo>> findAsync(RoutingId spotRid) {
        return lifecycle.spotManager().findAsync(spotRid);
    }

    @Override
    public CompletionStage<List<ZLinkSpotInfo>> listAsync() {
        return lifecycle.spotManager().listAsync();
    }

    @Override
    public CompletionStage<Boolean> removeAsync(RoutingId spotRid) {
        return lifecycle.spotManager().removeAsync(spotRid);
    }
}
