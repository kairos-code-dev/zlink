package systems.zlink.framework.actors;

import java.util.Optional;
import java.util.concurrent.CompletionStage;

public interface ZLinkActorManager {
    CompletionStage<ZLinkActor> createAsync(String actorId, String actorType);

    CompletionStage<Optional<ZLinkActor>> findAsync(String actorId);

    CompletionStage<ZLinkActor> getOrCreateAsync(String actorId, String actorType);
}
