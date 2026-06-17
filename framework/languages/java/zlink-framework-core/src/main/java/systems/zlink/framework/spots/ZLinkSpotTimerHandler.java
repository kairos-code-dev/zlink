package systems.zlink.framework.spots;

public interface ZLinkSpotTimerHandler<TSpot extends ZLinkSpot<?>> {
    void handle(TSpot spot, ZLinkTimerTick tick);
}
