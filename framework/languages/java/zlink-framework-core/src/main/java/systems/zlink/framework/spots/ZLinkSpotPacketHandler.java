package systems.zlink.framework.spots;

public interface ZLinkSpotPacketHandler<TSpot, TMessage> {
    void handle(TSpot spot, TMessage message);
}
