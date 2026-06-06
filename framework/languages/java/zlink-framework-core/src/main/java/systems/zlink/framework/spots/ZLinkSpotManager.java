package systems.zlink.framework.spots;

import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;

public interface ZLinkSpotManager {
    CompletionStage<ZLinkSpotCreateResult> createAsync(
        Class<? extends ZLinkSpot> spotType);

    CompletionStage<ZLinkSpotCreateResult> createAsync(
        Class<? extends ZLinkSpot> spotType,
        Message request);

    CompletionStage<ZLinkSpotCreateResult> createAsync(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid);

    CompletionStage<ZLinkSpotCreateResult> getOrCreateAsync(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid);

    CompletionStage<ZLinkSpotCreateResult> getOrCreateAsync(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid,
        Message request);

    CompletionStage<Optional<ZLinkSpotInfo>> findAsync(RoutingId spotRid);

    CompletionStage<List<ZLinkSpotInfo>> listAsync();

    CompletionStage<Boolean> closeAsync(RoutingId spotRid);
}
