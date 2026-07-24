package systems.zlink.framework.spring;

import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;

import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.actors.ZLinkActorCreateResult;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.messaging.ZLinkMessage;

final class ZLinkFrameworkActorManagerBean implements ZLinkActorManager {
    private final ZLinkFrameworkLifecycle lifecycle;

    ZLinkFrameworkActorManagerBean(ZLinkFrameworkLifecycle lifecycle) {
        this.lifecycle = lifecycle;
    }

    @Override
    public CompletionStage<ZLinkActorCreateResult> create(String actorId, String actorType) {
        return lifecycle.actorManager().create(actorId, actorType);
    }

    @Override
    public CompletionStage<ZLinkActorCreateResult> create(
        String actorId,
        String actorType,
        ZLinkMessage createRequest) {
        return lifecycle.actorManager().create(actorId, actorType, createRequest);
    }

    @Override
    public CompletionStage<Optional<ActorRef>> find(String actorId) {
        return lifecycle.actorManager().find(actorId);
    }

    @Override
    public CompletionStage<ZLinkActorCreateResult> getOrCreate(String actorId, String actorType) {
        return lifecycle.actorManager().getOrCreate(actorId, actorType);
    }

    @Override
    public CompletionStage<ZLinkActorCreateResult> getOrCreate(
        String actorId,
        String actorType,
        ZLinkMessage createRequest) {
        return lifecycle.actorManager().getOrCreate(actorId, actorType, createRequest);
    }
}
