package systems.zlink.framework.spots;

import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;

public interface ZLinkSpotManager {
    CompletionStage<ZLinkSpotCreateResult> createAsync(
        Class<? extends ZLinkSpot> spotType);

    CompletionStage<ZLinkSpotCreateResult> createAsync(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid);

    CompletionStage<ZLinkSpotCreateResult> getOrCreateAsync(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid);

    CompletionStage<Optional<ZLinkSpotInfo>> findAsync(RoutingId spotRid);

    CompletionStage<List<ZLinkSpotInfo>> listAsync();

    CompletionStage<Boolean> removeAsync(RoutingId spotRid);
}
