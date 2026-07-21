package systems.zlink.framework.spots;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkSendContext;

public interface ZLinkSpotPacketHandler<TSpot, TMessage> {
    CompletionStage<Void> handle(TSpot spot, TMessage message);

    default CompletionStage<Void> handle(
        TSpot spot,
        TMessage message,
        ZLinkSendContext context) {
        return handle(spot, message);
    }
}
