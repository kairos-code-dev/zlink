package systems.zlink.framework.streams;

import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorRef;

public interface ZLinkSessionActors {
    List<ZLinkSessionActor> bound();

    CompletionStage<ZLinkSessionActor> bind(ZLinkActor actor);

    CompletionStage<ZLinkSessionActor> bind(ZLinkActorRef actor);

    CompletionStage<ZLinkSessionActor> bindOrGet(ZLinkActorRef actor);

    Optional<ZLinkSessionActor> find(String actorId);
}
