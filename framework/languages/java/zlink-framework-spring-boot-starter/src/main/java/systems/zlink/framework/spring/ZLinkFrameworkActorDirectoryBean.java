package systems.zlink.framework.spring;

import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.actors.ZLinkActorDirectory;
import systems.zlink.framework.actors.ZLinkActorPlacement;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;

final class ZLinkFrameworkActorDirectoryBean implements ZLinkActorDirectory {
    private final ZLinkFrameworkLifecycle lifecycle;

    ZLinkFrameworkActorDirectoryBean(ZLinkFrameworkLifecycle lifecycle) {
        this.lifecycle = lifecycle;
    }

    @Override
    public CompletionStage<Optional<ZLinkActorRef>> find(String actorId) {
        return lifecycle.actorDirectory().find(actorId);
    }

    @Override
    public CompletionStage<ZLinkActorRef> ensure(
        String actorId,
        ZLinkMessage createRequest,
        ZLinkActorPlacement placement) {
        return lifecycle.actorDirectory().ensure(actorId, createRequest, placement);
    }
}
