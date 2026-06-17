package systems.zlink.framework.spring;

import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;

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
    public CompletionStage<ZLinkSpotCreateResult> create(
        Class<? extends ZLinkSpot<?>> spotType) {
        return lifecycle.spotManager().create(spotType);
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> create(
        Class<? extends ZLinkSpot<?>> spotType,
        Message request) {
        return lifecycle.spotManager().create(spotType, request);
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> create(
        Class<? extends ZLinkSpot<?>> spotType,
        RoutingId spotRid) {
        return lifecycle.spotManager().create(spotType, spotRid);
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> getOrCreate(
        Class<? extends ZLinkSpot<?>> spotType,
        RoutingId spotRid) {
        return lifecycle.spotManager().getOrCreate(spotType, spotRid);
    }

    @Override
    public CompletionStage<ZLinkSpotCreateResult> getOrCreate(
        Class<? extends ZLinkSpot<?>> spotType,
        RoutingId spotRid,
        Message request) {
        return lifecycle.spotManager().getOrCreate(spotType, spotRid, request);
    }

    @Override
    public CompletionStage<Optional<ZLinkSpotInfo>> find(RoutingId spotRid) {
        return lifecycle.spotManager().find(spotRid);
    }

    @Override
    public CompletionStage<List<ZLinkSpotInfo>> list() {
        return lifecycle.spotManager().list();
    }

    @Override
    public CompletionStage<Boolean> close(RoutingId spotRid) {
        return lifecycle.spotManager().close(spotRid);
    }
}
