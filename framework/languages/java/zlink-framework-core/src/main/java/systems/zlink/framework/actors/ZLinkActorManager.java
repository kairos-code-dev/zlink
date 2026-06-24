package systems.zlink.framework.actors;

import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.messaging.ZLinkMessage;

public interface ZLinkActorManager {
    CompletionStage<ZLinkActorRef> create(String actorId, String actorType);

    default CompletionStage<ZLinkActorRef> create(
        String actorId,
        String actorType,
        Object createRequest) {
        return create(actorId, actorType, ZLinkMessage.of(createRequest));
    }

    CompletionStage<ZLinkActorRef> create(
        String actorId,
        String actorType,
        ZLinkMessage createRequest);

    CompletionStage<Optional<ZLinkActorRef>> find(String actorId);

    CompletionStage<ZLinkActorRef> getOrCreate(String actorId, String actorType);

    default CompletionStage<ZLinkActorRef> getOrCreate(
        String actorId,
        String actorType,
        Object createRequest) {
        return getOrCreate(actorId, actorType, ZLinkMessage.of(createRequest));
    }

    CompletionStage<ZLinkActorRef> getOrCreate(
        String actorId,
        String actorType,
        ZLinkMessage createRequest);
}
