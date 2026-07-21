package systems.zlink.framework.runtime.spots;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.execution.ZLinkWorkerPool;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalAsyncSpotDispatchHandler;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerStages;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;

final class ZLinkSpotActivationFactory {
    private final ZLinkSpotRuntime host;
    private final ZLinkWorkerPool workerPool;
    private final ZLinkSpotHandlerLoader handlerLoader;
    private final ZLinkSpotHandlerInvoker handlerInvoker;
    private final ZLinkHandlerActivator handlerFactory;

    ZLinkSpotActivationFactory(
        ZLinkSpotRuntime host,
        ZLinkWorkerPool workerPool,
        ZLinkSpotHandlerLoader handlerLoader,
        ZLinkSpotHandlerInvoker handlerInvoker,
        ZLinkHandlerActivator handlerFactory) {
        this.host = host;
        this.workerPool = workerPool;
        this.handlerLoader = handlerLoader;
        this.handlerInvoker = handlerInvoker;
        this.handlerFactory = handlerFactory;
    }

    CompletionStage<SpotActivationCreateResult> activate(
        Class<? extends ZLinkSpot<?>> spotType,
        ZLinkBackendSpot backendSpot,
        ZLinkMessage request) {
        ZLinkMessage effectiveRequest = request == null ? ZLinkMessage.empty() : request;
        DefaultSpotContext context = new DefaultSpotContext(
            host,
            workerPool,
            handlerLoader,
            host.primaryNode().routingId(),
            backendSpot);
        ZLinkSpot<?> spot = createSpot(spotType, context);
        if (spot == null) {
            return CompletableFuture.completedFuture(new SpotActivationCreateResult(
                new SpotActivation(host, handlerInvoker, null, backendSpot, context),
                ZLinkSpotCreateResponse.accept()));
        }
        context.setSpot(spot);
        spot.configure();
        context.closeRegistration();
        context.bindSubscriptions(backendSpot);
        return host.runWithOutbound(context.dispatchOutbound(), () ->
                ZLinkHandlerStages.fromStageSupplier(() -> spot.onCreate(effectiveRequest)))
            .thenCompose(response -> initializeAcceptedSpot(
                spot,
                backendSpot,
                context,
                response))
            .handle((activation, error) -> {
                if (error == null) {
                    return activation;
                }
                context.closeTimers();
                backendSpot.close();
                throw new CompletionException(error);
            });
    }

    EntrySpotActivation activateEntry(
        RoutingId nodeRid,
        ZLinkBackendSpot backendSpot,
        Class<? extends ZLinkEntrySpot<?>> entrySpotType) {
        DefaultEntrySpotContext context = new DefaultEntrySpotContext(
            host,
            workerPool,
            handlerLoader,
            nodeRid,
            backendSpot);
        ZLinkEntrySpot<?> entrySpot = createEntrySpot(entrySpotType, context);
        if (entrySpot == null) {
            backendSpot.close();
            throw new ZLinkConfigurationException(
                "entry spot requires a public constructor accepting ZLinkEntrySpotContext "
                    + "or a public no-arg constructor: " + entrySpotType.getName());
        }
        if (entrySpot.context() != context) {
            backendSpot.close();
            throw new ZLinkConfigurationException(
                "entry spot must expose the context provided by the runtime: "
                    + entrySpotType.getName());
        }
        context.setEntrySpot(entrySpot);
        entrySpot.configure();
        context.closeRegistration();
        context.bindSubscriptions(backendSpot);
        context.enqueueDispatch(() -> host.runWithOutbound(
                context.dispatchOutbound(),
                () -> ZLinkHandlerStages.fromRunnable(entrySpot::onInitialize)))
            .whenComplete((ignored, error) -> {
                if (error != null) {
                    context.closeTimers();
                    backendSpot.close();
                }
            });
        EntrySpotActivation activation = new EntrySpotActivation(
            host,
            handlerInvoker,
            entrySpot,
            backendSpot,
            context);
        registerDispatchHandler(backendSpot, activation::handleDispatchEvent);
        return activation;
    }

    private CompletionStage<SpotActivationCreateResult> initializeAcceptedSpot(
        ZLinkSpot<?> spot,
        ZLinkBackendSpot backendSpot,
        DefaultSpotContext context,
        ZLinkSpotCreateResponse response) {
        ZLinkSpotCreateResponse effectiveResponse =
            response == null ? ZLinkSpotCreateResponse.accept() : response;
        if (!effectiveResponse.accepted()) {
            context.closeTimers();
            backendSpot.close();
            return CompletableFuture.completedFuture(
                new SpotActivationCreateResult(null, effectiveResponse));
        }
        return host.runWithOutbound(
                context.dispatchOutbound(),
                () -> ZLinkHandlerStages.fromStageSupplier(spot::onInitialize))
            .thenApply(ignored -> {
                SpotActivation activation = new SpotActivation(
                    host,
                    handlerInvoker,
                    spot,
                    backendSpot,
                    context);
                registerDispatchHandler(backendSpot, activation::handleDispatchEvent);
                return new SpotActivationCreateResult(activation, effectiveResponse);
            });
    }

    private static void registerDispatchHandler(
        ZLinkBackendSpot backendSpot,
        java.util.function.Function<
            systems.zlink.framework.runtime.backend.ZLinkBackendSpotDispatchInfo,
            CompletionStage<Void>> handler) {
        backendSpot.onDispatchEvent(new ZLinkInternalAsyncSpotDispatchHandler() {
            @Override
            public CompletionStage<Void> handleAsync(
                systems.zlink.framework.runtime.backend.ZLinkBackendSpotDispatchInfo info) {
                return handler.apply(info);
            }
        });
    }

    private ZLinkSpot<?> createSpot(
        Class<? extends ZLinkSpot<?>> spotType,
        ZLinkSpotContext context) {
        try {
            return (ZLinkSpot<?>) ZLinkHandlerActivator.services(handlerFactory)
                .add(ZLinkSpotContext.class, context)
                .create(spotType);
        } catch (RuntimeException error) {
            throw new ZLinkConfigurationException(
                "failed to create spot: " + spotType.getName(),
                error);
        }
    }

    private ZLinkEntrySpot<?> createEntrySpot(
        Class<? extends ZLinkEntrySpot<?>> entrySpotType,
        ZLinkEntrySpotContext context) {
        try {
            return (ZLinkEntrySpot<?>) ZLinkHandlerActivator.services(handlerFactory)
                .add(ZLinkEntrySpotContext.class, context)
                .create(entrySpotType);
        } catch (RuntimeException error) {
            throw new ZLinkConfigurationException(
                "failed to create entry spot: " + entrySpotType.getName(),
                error);
        }
    }
}
