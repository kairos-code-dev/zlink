package systems.zlink.framework.runtime.internal.spots;

import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.spots.SpotHandle;

public interface SpotTransportAddressResolver {
    CompletionStage<Optional<SpotTransportAddress>> resolve(SpotHandle handle);
}
