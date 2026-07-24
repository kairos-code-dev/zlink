package systems.zlink.framework.spring;

import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;

import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.SpotRef;
import systems.zlink.framework.spots.ZLinkSpotCreateCall;
import systems.zlink.framework.spots.ZLinkSpotGetOrCreateCall;
import systems.zlink.framework.spots.ZLinkSpotManager;

final class ZLinkFrameworkSpotManagerBean implements ZLinkSpotManager {
    private final ZLinkFrameworkLifecycle lifecycle;

    ZLinkFrameworkSpotManagerBean(ZLinkFrameworkLifecycle lifecycle) {
        this.lifecycle = lifecycle;
    }

    @Override
    public ZLinkSpotCreateCall create(String spotType) {
        return lifecycle.spotManager().create(spotType);
    }

    @Override
    public ZLinkSpotGetOrCreateCall getOrCreate(
        RoutingId spotRid,
        String spotType) {
        return lifecycle.spotManager().getOrCreate(spotRid, spotType);
    }

    @Override
    public CompletionStage<Optional<SpotRef>> find(RoutingId spotRid) {
        return lifecycle.spotManager().find(spotRid);
    }

    @Override
    public CompletionStage<Boolean> close(SpotRef spot) {
        return lifecycle.spotManager().close(spot);
    }
}
