package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;

public interface ZLinkActorAddressResolver {
    CompletionStage<ZLinkSpotAddress> resolveActorSpotAddressAsync(
        String actorId);
}
