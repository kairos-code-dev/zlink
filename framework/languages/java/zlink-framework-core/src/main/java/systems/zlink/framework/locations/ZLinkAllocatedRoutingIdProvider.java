package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;

public interface ZLinkAllocatedRoutingIdProvider {
    CompletionStage<ZLinkAllocatedRoutingId> waitForReadyAllocation(String groupName);
}
