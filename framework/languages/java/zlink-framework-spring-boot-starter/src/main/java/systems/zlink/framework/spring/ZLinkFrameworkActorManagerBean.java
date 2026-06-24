package systems.zlink.framework.spring;

import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;

import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.messaging.ZLinkMessage;

final class ZLinkFrameworkActorManagerBean implements ZLinkActorManager {
    private final ZLinkFrameworkLifecycle lifecycle;

    ZLinkFrameworkActorManagerBean(ZLinkFrameworkLifecycle lifecycle) {
        this.lifecycle = lifecycle;
    }

    @Override
    public CompletionStage<ZLinkActorRef> create(String actorId, String actorType) {
        return lifecycle.actorManager().create(actorId, actorType);
    }

    @Override
    public CompletionStage<ZLinkActorRef> create(
        String actorId,
        String actorType,
        ZLinkMessage createRequest) {
        return lifecycle.actorManager().create(actorId, actorType, createRequest);
    }

    @Override
    public CompletionStage<Optional<ZLinkActorRef>> find(String actorId) {
        return lifecycle.actorManager().find(actorId);
    }

    @Override
    public CompletionStage<ZLinkActorRef> getOrCreate(String actorId, String actorType) {
        return lifecycle.actorManager().getOrCreate(actorId, actorType);
    }

    @Override
    public CompletionStage<ZLinkActorRef> getOrCreate(
        String actorId,
        String actorType,
        ZLinkMessage createRequest) {
        return lifecycle.actorManager().getOrCreate(actorId, actorType, createRequest);
    }
}
