package systems.zlink.framework.actors;

import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.messaging.ZLinkMessage;

public interface ZLinkActorDirectory {
    CompletionStage<Optional<ZLinkActorRef>> find(String actorId);

    CompletionStage<ZLinkActorRef> ensure(
        String actorId,
        ZLinkMessage createRequest,
        ZLinkActorPlacement placement);

    default CompletionStage<ZLinkActorRef> ensure(
        String actorId,
        ZLinkMessage createRequest) {
        return ensure(actorId, createRequest, ZLinkActorPlacement.any());
    }

    default CompletionStage<ZLinkActorRef> ensure(
        String actorId,
        Object createRequest) {
        return ensure(actorId, ZLinkMessage.of(createRequest));
    }
}
