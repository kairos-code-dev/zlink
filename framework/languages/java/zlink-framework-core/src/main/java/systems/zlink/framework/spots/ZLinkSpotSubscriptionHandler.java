package systems.zlink.framework.spots;

public interface ZLinkSpotSubscriptionHandler<TSpot, TEvent> {
    void handle(TSpot spot, TEvent message);
}
