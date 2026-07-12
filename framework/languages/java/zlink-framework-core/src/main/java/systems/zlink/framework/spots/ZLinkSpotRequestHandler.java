package systems.zlink.framework.spots;

import java.util.concurrent.CompletionStage;

public interface ZLinkSpotRequestHandler<TSpot, TRequest, TReply> {
    CompletionStage<TReply> handle(TSpot spot, TRequest request);
}
