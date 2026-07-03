package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;

public interface ZLinkRouteLocationResolver {
    CompletionStage<ZLinkRouteLocation> resolveRouteAsync(ZLinkRouteLocationKey key);
}
