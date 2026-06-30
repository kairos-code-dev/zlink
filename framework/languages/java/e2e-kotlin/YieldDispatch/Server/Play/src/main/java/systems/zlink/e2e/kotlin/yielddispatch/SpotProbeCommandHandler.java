package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.spots.ZLinkSpotPacketHandler;

public final class SpotProbeCommandHandler
    implements ZLinkSpotPacketHandler<ProbeSpot, Contracts.SpotProbeCommand> {
    private final PlayEvidenceStore evidence;

    public SpotProbeCommandHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @Override
    public void handle(
        ProbeSpot spot,
        Contracts.SpotProbeCommand command) {
        String value = "spot=" + spot.context().spotRid()
            + ";node=" + spot.context().nodeRid()
            + ";marker=" + command.marker();
        evidence.record(command.requestId(), "probe-completed", value);
    }
}
