package systems.zlink.framework.spots;

import java.util.concurrent.CompletionStage;

public interface ZLinkSpotSubscriptionHandler<TSpot, TEvent> {
    CompletionStage<Void> handleAsync(TSpot spot, TEvent message);
}
