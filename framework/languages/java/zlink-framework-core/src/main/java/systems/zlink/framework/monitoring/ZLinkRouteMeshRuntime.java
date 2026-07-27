package systems.zlink.framework.monitoring;

import java.util.concurrent.Flow;

public interface ZLinkRouteMeshRuntime {
    ZLinkMeshNodeSnapshot snapshot(String meshName);

    Flow.Publisher<ZLinkMeshRuntimeEvent> observe(String meshName, int capacity);

    boolean isReady(String meshName);

}
