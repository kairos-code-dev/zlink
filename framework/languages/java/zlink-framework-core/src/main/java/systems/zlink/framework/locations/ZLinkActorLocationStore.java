package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;

public interface ZLinkActorLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updateActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent);

    CompletionStage<ZLinkLocationWriteResult> removeActorAsync(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner);

    CompletionStage<ZLinkActorLocation> resolveActorAsync(ZLinkActorLocationKey key);

    CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> listActorLocationsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page);
}
