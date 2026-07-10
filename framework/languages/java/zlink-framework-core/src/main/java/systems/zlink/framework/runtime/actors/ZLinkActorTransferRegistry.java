package systems.zlink.framework.runtime.actors;

import java.util.Map;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorTransferAdapter;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;

final class ZLinkActorTransferRegistry {
    private final Map<String, Class<? extends ZLinkActorTransferAdapter<?>>> adapters;
    private final ZLinkHandlerFactory handlerFactory;

    ZLinkActorTransferRegistry(
        Map<String, Class<? extends ZLinkActorTransferAdapter<?>>> adapters,
        ZLinkHandlerFactory handlerFactory) {
        this.adapters = adapters == null ? Map.of() : Map.copyOf(adapters);
        this.handlerFactory = handlerFactory;
    }

    TransferState transferOut(
        String actorType,
        ZLinkActor actor,
        CancellationToken cancellationToken) {
        Class<? extends ZLinkActorTransferAdapter<?>> adapterType = adapters.get(actorType);
        if (adapterType == null) {
            return new TransferState(null, ZLinkMessage.empty());
        }
        ZLinkMessage state = invokeTransferOut(
            createAdapter(adapterType),
            actor,
            cancellationToken);
        if (state == null) {
            throw new ZLinkConfigurationException(
                "actor transfer adapter returned a null state: " + adapterType.getName());
        }
        return new TransferState(actorType, state);
    }

    ZLinkActor transferIn(
        String actorType,
        String actorId,
        ZLinkActorContext context,
        ZLinkMessage state,
        CancellationToken cancellationToken) {
        Class<? extends ZLinkActorTransferAdapter<?>> adapterType = adapters.get(actorType);
        if (adapterType == null) {
            return null;
        }
        ZLinkActor actor = invokeTransferIn(
            createAdapter(adapterType),
            actorId,
            context,
            state,
            cancellationToken);
        if (actor == null || !actorId.equals(actor.actorId())) {
            throw new ZLinkConfigurationException(
                "actor transfer adapter returned an actor with a different id: "
                    + adapterType.getName());
        }
        return actor;
    }

    private ZLinkActorTransferAdapter<?> createAdapter(
        Class<? extends ZLinkActorTransferAdapter<?>> adapterType) {
        return (ZLinkActorTransferAdapter<?>) handlerFactory.create(adapterType);
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private static ZLinkMessage invokeTransferOut(
        ZLinkActorTransferAdapter adapter,
        ZLinkActor actor,
        CancellationToken cancellationToken) {
        return adapter.transferOut(actor, cancellationToken);
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private static ZLinkActor invokeTransferIn(
        ZLinkActorTransferAdapter adapter,
        String actorId,
        ZLinkActorContext context,
        ZLinkMessage state,
        CancellationToken cancellationToken) {
        return (ZLinkActor) adapter.transferIn(actorId, context, state, cancellationToken);
    }

    record TransferState(String adapterKey, ZLinkMessage state) {
    }
}
