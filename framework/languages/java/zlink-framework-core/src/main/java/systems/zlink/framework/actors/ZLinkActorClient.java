package systems.zlink.framework.actors;

public interface ZLinkActorClient {
    ZLinkActorSendCall sendToActor(ActorRef actorRef, Object message);

    ZLinkActorRequestCall requestToActor(ActorRef actorRef, Object request);
}
