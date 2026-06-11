package systems.zlink.framework.spots;

import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;

public interface ZLinkSpotManager {
    CompletionStage<ZLinkSpotCreateResult> create(
        Class<? extends ZLinkSpot> spotType);

    CompletionStage<ZLinkSpotCreateResult> create(
        Class<? extends ZLinkSpot> spotType,
        Message request);

    CompletionStage<ZLinkSpotCreateResult> create(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid);

    CompletionStage<ZLinkSpotCreateResult> getOrCreate(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid);

    CompletionStage<ZLinkSpotCreateResult> getOrCreate(
        Class<? extends ZLinkSpot> spotType,
        RoutingId spotRid,
        Message request);

    CompletionStage<Optional<ZLinkSpotInfo>> find(RoutingId spotRid);

    CompletionStage<List<ZLinkSpotInfo>> list();

    CompletionStage<Boolean> close(RoutingId spotRid);
}
