package systems.zlink.framework.spots;

public interface ZLinkSpotHandlerRegistry {
    default void addHandler(Class<?> handlerType) {
        addPacket(handlerType);
    }

    void addPacket(Class<?> handlerType);

    void addSubscribe(String topic, Class<?> handlerType);

    default void addActorPacket(Class<?> handlerType) {
        addHandler(handlerType);
    }

    void addActorSend(Class<?> handlerType);

    void addActorRequest(Class<?> handlerType);
}
