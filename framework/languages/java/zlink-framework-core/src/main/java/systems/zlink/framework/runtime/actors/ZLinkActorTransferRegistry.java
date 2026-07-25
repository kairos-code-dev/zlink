package systems.zlink.framework.runtime.actors;

import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorTransferAdapter;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;

final class ZLinkActorTransferRegistry {
    private final Map<String, Class<? extends ZLinkActorTransferAdapter<?>>> adapters;
    private final ZLinkHandlerActivator handlerFactory;

    ZLinkActorTransferRegistry(
        Map<String, Class<? extends ZLinkActorTransferAdapter<?>>> adapters,
        ZLinkHandlerActivator handlerFactory) {
        this.adapters = adapters == null ? Map.of() : Map.copyOf(adapters);
        this.handlerFactory = handlerFactory;
    }

    CompletionStage<TransferState> transferOut(String actorType, ZLinkActor actor) {
        Class<? extends ZLinkActorTransferAdapter<?>> adapterType = adapters.get(actorType);
        if (adapterType == null) {
            return CompletableFuture.completedFuture(
                new TransferState(null, ZLinkMessage.empty()));
        }
        return invokeTransferOut(createAdapter(adapterType), actor)
            .thenApply(state -> {
                if (state == null) {
                    throw new ZLinkConfigurationException(
                        "actor transfer adapter returned a null state: " + adapterType.getName());
                }
                return new TransferState(actorType, state);
            });
    }

    CompletionStage<ZLinkActor> transferIn(
        String actorType,
        String actorId,
        ZLinkActorContext context,
        ZLinkMessage state) {
        Class<? extends ZLinkActorTransferAdapter<?>> adapterType = adapters.get(actorType);
        if (adapterType == null) {
            return CompletableFuture.completedFuture(null);
        }
        return invokeTransferIn(createAdapter(adapterType), actorId, context, state)
            .thenApply(actor -> {
                if (actor == null || !actorId.equals(actor.context().actorId())) {
                    throw new ZLinkConfigurationException(
                        "actor transfer adapter returned an actor with a different id: "
                            + adapterType.getName());
                }
                return actor;
            });
    }

    private ZLinkActorTransferAdapter<?> createAdapter(
        Class<? extends ZLinkActorTransferAdapter<?>> adapterType) {
        return (ZLinkActorTransferAdapter<?>) handlerFactory.create(adapterType);
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private static CompletionStage<ZLinkMessage> invokeTransferOut(
        ZLinkActorTransferAdapter adapter,
        ZLinkActor actor) {
        return adapter.transferOut(actor);
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private static CompletionStage<ZLinkActor> invokeTransferIn(
        ZLinkActorTransferAdapter adapter,
        String actorId,
        ZLinkActorContext context,
        ZLinkMessage state) {
        return adapter.transferIn(actorId, context, state);
    }

    record TransferState(String adapterKey, ZLinkMessage state) {
    }
}
