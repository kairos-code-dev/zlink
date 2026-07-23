package systems.zlink.framework.locations;

import java.util.Objects;

public record ZLinkAggregateAlreadyPrepared(ZLinkAggregateFence fence)
    implements ZLinkAggregatePrepareResult {
    public ZLinkAggregateAlreadyPrepared {
        Objects.requireNonNull(fence, "fence");
    }
}
