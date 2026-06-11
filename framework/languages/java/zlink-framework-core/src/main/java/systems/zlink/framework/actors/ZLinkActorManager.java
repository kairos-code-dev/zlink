package systems.zlink.framework.actors;

import java.util.Optional;
import java.util.concurrent.CompletionStage;

public interface ZLinkActorManager {
    CompletionStage<ZLinkActor> create(String actorId, String actorType);

    CompletionStage<Optional<ZLinkActor>> find(String actorId);

    CompletionStage<ZLinkActor> getOrCreate(String actorId, String actorType);
}
