package systems.zlink.framework.spots;

import java.util.concurrent.CompletionStage;

public interface ZLinkSpotPacketHandler<TSpot, TMessage> {
    CompletionStage<Void> handleAsync(TSpot spot, TMessage message);
}
