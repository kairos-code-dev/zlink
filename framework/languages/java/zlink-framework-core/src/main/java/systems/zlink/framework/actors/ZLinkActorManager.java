package systems.zlink.framework.actors;

import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.messaging.ZLinkMessage;

public interface ZLinkActorManager {
    /**
     * Creates an actor with a globally unique actor id. The actor type string is
     * used only by creation APIs and must match a value registered with
     * {@code addActorFactory}.
     */
    CompletionStage<ZLinkActorCreateResult> create(String actorId, String actorType);

    /**
     * Creates an actor with a creation payload. The actor type string is defined
     * by factory registration; lookup APIs use actor id only.
     */
    default CompletionStage<ZLinkActorCreateResult> create(
        String actorId,
        String actorType,
        Object createRequest) {
        return create(actorId, actorType, ZLinkMessage.of(createRequest));
    }

    /**
     * Creates an actor with a framework message payload. The actor type string is
     * defined by factory registration; lookup APIs use actor id only.
     */
    CompletionStage<ZLinkActorCreateResult> create(
        String actorId,
        String actorType,
        ZLinkMessage createRequest);

    /**
     * Looks up an existing actor by its globally unique actor id. Actor type is
     * not part of the lookup key.
     */
    CompletionStage<Optional<ActorRef>> find(String actorId);

    /**
     * Finds an actor by id or creates it using the factory registered for the
     * supplied actor type string.
     */
    CompletionStage<ZLinkActorCreateResult> getOrCreate(String actorId, String actorType);

    /**
     * Finds an actor by id or creates it with a creation payload. The actor type
     * string must be one registered with {@code addActorFactory}.
     */
    default CompletionStage<ZLinkActorCreateResult> getOrCreate(
        String actorId,
        String actorType,
        Object createRequest) {
        return getOrCreate(actorId, actorType, ZLinkMessage.of(createRequest));
    }

    /**
     * Finds an actor by id or creates it with a framework message payload. The
     * actor type string must be one registered with {@code addActorFactory}.
     */
    CompletionStage<ZLinkActorCreateResult> getOrCreate(
        String actorId,
        String actorType,
        ZLinkMessage createRequest);
}
