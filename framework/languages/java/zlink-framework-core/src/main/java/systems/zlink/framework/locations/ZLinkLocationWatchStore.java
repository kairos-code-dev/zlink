package systems.zlink.framework.locations;

import java.util.concurrent.Flow;

public interface ZLinkLocationWatchStore {
    Flow.Publisher<ZLinkLocationChanged> watch(ZLinkLocationWatchFilter filter);
}
