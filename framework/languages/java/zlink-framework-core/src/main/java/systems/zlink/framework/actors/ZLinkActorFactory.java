package systems.zlink.framework.actors;

import java.util.concurrent.CompletionStage;

public interface ZLinkActorFactory {
    CompletionStage<ZLinkActor> createAsync(
        String actorId,
        ZLinkActorContext context);
}
