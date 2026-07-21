package systems.zlink.framework.spots;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkPublishContext;

public interface ZLinkSpotSubscriptionHandler<TSpot, TEvent> {
    CompletionStage<Void> handle(TSpot spot, TEvent message);

    default CompletionStage<Void> handle(
        TSpot spot,
        TEvent message,
        ZLinkPublishContext context) {
        return handle(spot, message);
    }
}
