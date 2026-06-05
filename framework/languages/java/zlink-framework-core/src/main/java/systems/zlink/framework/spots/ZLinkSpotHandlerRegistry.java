package systems.zlink.framework.spots;

public interface ZLinkSpotHandlerRegistry {
    default void addHandler(Class<?> handlerType) {
        addPacket(handlerType);
    }

    void addPacket(Class<?> handlerType);

    void addSubscribe(String topic, Class<?> handlerType);

    default void addActorJoin(Class<?> handlerType) {
        addHandler(handlerType);
    }

    default void addActorPacket(Class<?> handlerType) {
        addHandler(handlerType);
    }

    default void addPostActorJoined(Class<?> handlerType) {
        addHandler(handlerType);
    }

    default void addActorLeft(Class<?> handlerType) {
        addHandler(handlerType);
    }

    default void addActorDisconnected(Class<?> handlerType) {
        addHandler(handlerType);
    }
}
