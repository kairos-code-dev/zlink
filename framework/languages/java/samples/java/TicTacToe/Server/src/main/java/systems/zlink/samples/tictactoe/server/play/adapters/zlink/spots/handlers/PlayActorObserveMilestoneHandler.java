package systems.zlink.samples.tictactoe.server.play.adapters.zlink.spots.handlers;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.play.adapters.zlink.actors.PlayActor;
import systems.zlink.samples.tictactoe.server.play.adapters.zlink.spots.PlayEntrySpot;
import systems.zlink.samples.tictactoe.shared.contracts.ObserveMilestoneReq;
import systems.zlink.samples.tictactoe.shared.contracts.ObserveMilestoneRes;

@ZLinkHandlerGroup(SampleNames.PlayActor)
public final class PlayActorObserveMilestoneHandler {
    @ZLinkSpotActorRequest
    public ObserveMilestoneRes observe(
        PlayEntrySpot entrySpot,
        PlayActor actor,
        ZLinkSpotActorRequestContext context,
        ObserveMilestoneReq request,
        CancellationToken cancellationToken) {
        return entrySpot.observeMilestone(actor);
    }
}
