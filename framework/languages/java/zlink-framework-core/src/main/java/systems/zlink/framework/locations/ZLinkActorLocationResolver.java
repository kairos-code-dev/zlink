package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;

public interface ZLinkActorLocationResolver {
    CompletionStage<ZLinkSpotAddress> resolveActorSpotAddressAsync(
        String actorType,
        String actorId);
}
