package systems.zlink.framework.spots;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkRequestContext;

public interface ZLinkSpotRequestHandler<TSpot, TRequest, TReply> {
    CompletionStage<TReply> handle(TSpot spot, TRequest request);

    default CompletionStage<TReply> handle(
        TSpot spot,
        TRequest request,
        ZLinkRequestContext context) {
        return handle(spot, request);
    }
}
