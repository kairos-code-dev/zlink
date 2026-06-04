package systems.zlink.framework.spots;

public interface ZLinkSpotHandlerRegistry {
    default void addHandler(Class<?> handlerType) {
        addPacket(handlerType);
    }

    void addPacket(Class<?> handlerType);

    void addSubscribe(String topic, Class<?> handlerType);
}
