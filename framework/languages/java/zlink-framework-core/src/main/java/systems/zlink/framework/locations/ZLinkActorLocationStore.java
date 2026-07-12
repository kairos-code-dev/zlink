package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;

public interface ZLinkActorLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updateActor(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent);

    CompletionStage<ZLinkLocationWriteResult> removeActor(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner);

    CompletionStage<ZLinkActorLocation> resolveActor(ZLinkActorLocationKey key);

    CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> listActorLocations(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page);
}
