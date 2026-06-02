package systems.zlink.framework.runtime;

import java.lang.reflect.InvocationTargetException;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkBoundSession;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.spots.ZLinkSpot;

public final class ZLinkActorRuntime implements ZLinkActorManager {
    private final ZLinkBackendSpotNode spotNode;
    private final Map<String, Class<? extends ZLinkActorFactory>> factories;
    private final Map<String, ZLinkActor> actors = new HashMap<>();
    private final Map<ZLinkActor, ZLinkBackendActorRef> refsByActor = new HashMap<>();
    private final Map<ZLinkActor, DefaultActorContext> contextsByActor = new HashMap<>();

    ZLinkActorRuntime(
        ZLinkBackendSpotNode spotNode,
        Map<String, Class<? extends ZLinkActorFactory>> factories) {
        if (factories.isEmpty()) {
            throw new ZLinkConfigurationException("at least one actor factory is required");
        }
        this.spotNode = spotNode;
        this.factories = Map.copyOf(factories);
    }

    @Override
    public CompletionStage<ZLinkActor> createAsync(String actorId, String actorType) {
        requireActorId(actorId);
        Class<? extends ZLinkActorFactory> factoryType = requireFactory(actorType);
        if (actors.containsKey(actorId)) {
            throw new ZLinkConfigurationException("duplicate actor id: " + actorId);
        }
        ZLinkBackendActorRef actorRef = spotNode.createActor(actorId);
        DefaultActorContext context = new DefaultActorContext(actorRef);
        ZLinkActor actor = createFactory(factoryType)
            .createAsync(actorId, context)
            .toCompletableFuture()
            .join();
        actors.put(actorId, actor);
        refsByActor.put(actor, actorRef);
        contextsByActor.put(actor, context);
        return CompletableFuture.completedFuture(actor);
    }

    @Override
    public CompletionStage<Optional<ZLinkActor>> findAsync(String actorId) {
        requireActorId(actorId);
        if (!actors.containsKey(actorId)) {
            spotNode.actorLookup(actorId);
        }
        return CompletableFuture.completedFuture(Optional.ofNullable(actors.get(actorId)));
    }

    @Override
    public CompletionStage<ZLinkActor> getOrCreateAsync(String actorId, String actorType) {
        requireActorId(actorId);
        ZLinkActor actor = actors.get(actorId);
        if (actor != null) {
            return CompletableFuture.completedFuture(actor);
        }
        return createAsync(actorId, actorType);
    }

    private Class<? extends ZLinkActorFactory> requireFactory(String actorType) {
        if (actorType == null || actorType.isBlank()) {
            throw new ZLinkConfigurationException("actorType is required");
        }
        Class<? extends ZLinkActorFactory> factory = factories.get(actorType);
        if (factory == null) {
            throw new ZLinkConfigurationException("actor type is not registered: " + actorType);
        }
        return factory;
    }

    private static void requireActorId(String actorId) {
        if (actorId == null || actorId.isBlank()) {
            throw new ZLinkConfigurationException("actorId is required");
        }
    }

    private static ZLinkActorFactory createFactory(
        Class<? extends ZLinkActorFactory> factoryType) {
        try {
            return factoryType.getDeclaredConstructor().newInstance();
        } catch (InstantiationException
                 | IllegalAccessException
                 | InvocationTargetException
                 | NoSuchMethodException ex) {
            throw new ZLinkConfigurationException(
                "failed to create actor factory: " + factoryType.getName());
        }
    }

    ZLinkBackendActorRef refFor(ZLinkActor actor) {
        ZLinkBackendActorRef ref = refsByActor.get(actor);
        if (ref == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        return ref;
    }

    long bindSession(ZLinkActor actor, ZLinkBoundSession boundSession) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        return context.setBoundSession(boundSession);
    }

    void clearSessionBinding(ZLinkActor actor, long bindingToken) {
        DefaultActorContext context = contextsByActor.get(actor);
        if (context == null) {
            throw new ZLinkConfigurationException(
                "actor is not managed by this runtime: " + actor.actorId());
        }
        context.clearBoundSession(bindingToken);
    }

    private static final class DefaultActorContext implements ZLinkActorContext {
        private final ZLinkBackendActorRef actorRef;
        private ZLinkBoundSession boundSession;
        private long sessionBindingToken;

        DefaultActorContext(ZLinkBackendActorRef actorRef) {
            this.actorRef = actorRef;
        }

        @Override
        public Optional<RoutingId> spotRid() {
            return Optional.empty();
        }

        @Override
        public boolean isJoined() {
            return false;
        }

        @Override
        public ZLinkBoundSession boundSession() {
            if (boundSession == null) {
                throw new ZLinkConfigurationException("actor has no bound session");
            }
            return boundSession;
        }

        @Override
        public ZLinkSpot getSpot() {
            return null;
        }

        @Override
        public <TSpot extends ZLinkSpot> TSpot getSpot(Class<TSpot> spotType) {
            return null;
        }

        long setBoundSession(ZLinkBoundSession boundSession) {
            sessionBindingToken++;
            this.boundSession = boundSession;
            return sessionBindingToken;
        }

        void clearBoundSession(long bindingToken) {
            if (bindingToken == sessionBindingToken) {
                boundSession = null;
            }
        }
    }
}
