package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.spots.ZLinkSpotPacketHandler;

public final class WorkerYieldMsgHandler
    implements ZLinkSpotPacketHandler<ProbeSpot, Contracts.WorkerYieldMsg> {
    private final PlayEvidenceStore evidence;

    public WorkerYieldMsgHandler(PlayEvidenceStore evidence) {
        this.evidence = evidence;
    }

    @Override
    public void handle(
        ProbeSpot spot,
        Contracts.WorkerYieldMsg command) {
        String value = "spot=" + spot.context().spotRid()
            + ";handler=spot";
        evidence.record(command.requestId(), "worker-yield-started", value);
        var call = spot.context().runWorker(token -> {
                Thread.sleep(command.delayMillis());
                return command.requestId();
            })
            .timeout(Duration.ofSeconds(10));
        evidence.record(command.requestId(), "worker-yield-released", value);
        String result = call.yield();
        evidence.record(result, "worker-yield-resumed", value);
        evidence.record(result, "worker-yield-completed", value);
    }
}
