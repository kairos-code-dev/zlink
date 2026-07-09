package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;

public interface ActorSpotRefResolver {
    CompletionStage<SpotRef> resolveActorSpotRefAsync(String actorId);
}
