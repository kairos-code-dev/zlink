package systems.zlink.framework.runtime.internal.spots;

import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.spots.SpotHandle;

public interface SpotTransportAddressResolver {
    default CompletionStage<Optional<SpotTransportAddress>> resolve(SpotHandle handle) {
        return resolve(handle.spotId());
    }

    CompletionStage<Optional<SpotTransportAddress>> resolve(String spotId);
}
