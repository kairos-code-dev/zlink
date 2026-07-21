package systems.zlink.framework.monitoring;

import java.time.Duration;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.Flow;

public interface ZLinkRouteMeshRuntime {
    ZLinkMeshNodeSnapshot snapshot(String meshName);

    Flow.Publisher<ZLinkMeshRuntimeEvent> observe(String meshName, int capacity);

    boolean isReady(String meshName);

    CompletionStage<ZLinkMeshDrainResult> drain(String meshName, Duration deadline);

    CompletionStage<ZLinkMeshDrainResult> awaitDrained(String meshName);
}
