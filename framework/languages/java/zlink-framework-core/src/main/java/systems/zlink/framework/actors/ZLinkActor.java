package systems.zlink.framework.actors;

public interface ZLinkActor {
    String actorId();

    ZLinkActorContext context();

    default void configure() {
    }
}
