package systems.zlink.framework.spring;

import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.actors.ZLinkActorClient;
import systems.zlink.framework.actors.ZLinkActorRequestCall;
import systems.zlink.framework.actors.ZLinkActorSendCall;
import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;

final class ZLinkFrameworkActorClientBean implements ZLinkActorClient {
    private final ZLinkFrameworkLifecycle lifecycle;

    ZLinkFrameworkActorClientBean(ZLinkFrameworkLifecycle lifecycle) {
        this.lifecycle = lifecycle;
    }

    @Override
    public ZLinkActorSendCall sendToActor(ActorRef actorRef, Object message) {
        return lifecycle.actorClient().sendToActor(actorRef, message);
    }

    @Override
    public ZLinkActorRequestCall requestToActor(ActorRef actorRef, Object request) {
        return lifecycle.actorClient().requestToActor(actorRef, request);
    }
}
