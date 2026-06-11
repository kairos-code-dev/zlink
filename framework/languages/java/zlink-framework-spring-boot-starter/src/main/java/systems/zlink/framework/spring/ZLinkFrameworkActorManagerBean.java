package systems.zlink.framework.spring;

import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;

import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorManager;

final class ZLinkFrameworkActorManagerBean implements ZLinkActorManager {
    private final ZLinkFrameworkLifecycle lifecycle;

    ZLinkFrameworkActorManagerBean(ZLinkFrameworkLifecycle lifecycle) {
        this.lifecycle = lifecycle;
    }

    @Override
    public CompletionStage<ZLinkActor> create(String actorId, String actorType) {
        return lifecycle.actorManager().create(actorId, actorType);
    }

    @Override
    public CompletionStage<Optional<ZLinkActor>> find(String actorId) {
        return lifecycle.actorManager().find(actorId);
    }

    @Override
    public CompletionStage<ZLinkActor> getOrCreate(String actorId, String actorType) {
        return lifecycle.actorManager().getOrCreate(actorId, actorType);
    }
}
