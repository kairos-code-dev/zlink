package systems.zlink.framework.spots;

public interface ZLinkSpotRequestHandler<TSpot, TRequest, TReply> {
    TReply handle(TSpot spot, TRequest request);
}
