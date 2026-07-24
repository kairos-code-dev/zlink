package systems.zlink.framework.runtime.spots;

import java.util.concurrent.CompletionStage;
import java.util.function.Supplier;
import systems.zlink.contracts.core.RoutingId;

interface SpotDispatchLine {
    CompletionStage<Void> enqueueDispatch(Supplier<CompletionStage<Void>> operation);

    String spotId();

    DefaultSpotOutbound dispatchOutbound();

    ZLinkSpotHandlerCatalog handlerCatalog();
}
