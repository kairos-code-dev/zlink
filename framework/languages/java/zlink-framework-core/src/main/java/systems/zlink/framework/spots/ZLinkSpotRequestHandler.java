package systems.zlink.framework.spots;

import java.util.concurrent.CompletionStage;

public interface ZLinkSpotRequestHandler<TSpot, TRequest, TReply> {
    CompletionStage<TReply> handleAsync(TSpot spot, TRequest request);
}
