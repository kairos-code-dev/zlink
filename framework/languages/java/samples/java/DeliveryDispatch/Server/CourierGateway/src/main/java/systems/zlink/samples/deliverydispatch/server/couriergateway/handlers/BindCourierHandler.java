package systems.zlink.samples.deliverydispatch.server.couriergateway.handlers;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.spots.SpotHandle;
import systems.zlink.framework.spots.SpotHandleResolver;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.server.couriergateway.CourierBinding;
import systems.zlink.samples.deliverydispatch.server.couriergateway.CourierDirectory;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

@ZLinkHandlerGroup("courier-gateway")
public final class BindCourierHandler
    implements ZLinkRequestHandler<Messages.BindCourierReq, Messages.BindCourierRes> {
    private final CourierDirectory directory;
    private final ZLinkRouteClient routes;
    private final SpotHandleResolver spotHandles;

    public BindCourierHandler(
        CourierDirectory directory,
        ZLinkRouteClient routes,
        SpotHandleResolver spotHandles) {
        this.directory = directory;
        this.routes = routes;
        this.spotHandles = spotHandles;
    }

    @Override
    public CompletionStage<Messages.BindCourierRes> handle(
        Messages.BindCourierReq request,
        ZLinkRequestContext context) {
        String placement = directory.choosePlacement(request.courierId());
        return resolveSpot(placement).thenCompose(address -> routes
                .requestToSpot(
                    SampleNames.CourierSpotDiscovery,
                    address,
                    new Messages.FindCourierActorReq(request.courierId()))
                .timeout(SampleTimings.RequestTimeout)
                .submit(Messages.FindCourierActorRes.class)
                .thenCompose(found -> found.actor() == null
                    ? routes.requestToSpot(
                            SampleNames.CourierSpotDiscovery,
                            address,
                            new Messages.EnsureCourierActorReq(request.courierId()))
                        .timeout(SampleTimings.RequestTimeout)
                        .submit(Messages.EnsureCourierActorRes.class)
                    : CompletableFuture.completedFuture(
                        new Messages.EnsureCourierActorRes(request.courierId(), found.actor()))))
            .thenApply(ensured -> {
                CourierBinding binding = directory.remember(ensured, request.sessionRoute());
                return new Messages.BindCourierRes(
                    request.courierId(), binding.actor(), binding.sessionRoute());
            });
    }

    private CompletionStage<SpotHandle> resolveSpot(String placement) {
        return spotHandles.resolveSpotHandle(RoutingId.from(placement))
            .thenApply(found -> found.orElseThrow(() ->
                new IllegalStateException("courier Spot is not registered: " + placement)));
    }
}
