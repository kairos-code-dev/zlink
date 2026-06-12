package systems.zlink.framework.actors;

public interface ZLinkActorFactory {
    ZLinkActor create(
        String actorId,
        ZLinkActorContext context);
}
