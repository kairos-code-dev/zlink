package systems.zlink.framework.spots;

public interface ZLinkSpotHandlerRegistry {
    void addPacket(Class<?> handlerType);

    void addSubscribe(String topic, Class<?> handlerType);
}
