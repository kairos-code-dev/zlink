package systems.zlink.framework.monitoring;

import java.util.concurrent.Flow;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState;

public interface ZLinkRuntimeQuery {
    ZLinkFrameworkRuntimeState state();

    ZLinkFrameworkRuntimeSnapshot snapshot();

    Flow.Publisher<ZLinkFrameworkRuntimeEvent> observe(int capacity);
}
