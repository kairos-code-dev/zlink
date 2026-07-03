package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;

public interface ZLinkActorLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updateActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent);

    CompletionStage<ZLinkLocationWriteResult> removeActorAsync(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner);

    CompletionStage<Long> removeActorsByOwnerAsync(String ownerId);

    CompletionStage<ZLinkActorLocation> resolveActorAsync(ZLinkActorLocationKey key);

    CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> listActorsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page);
}
