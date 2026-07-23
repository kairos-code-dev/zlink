package systems.zlink.framework.locations;

import java.util.Objects;

public record ZLinkAggregatePrepared(ZLinkAggregateFence fence)
    implements ZLinkAggregatePrepareResult {
    public ZLinkAggregatePrepared {
        Objects.requireNonNull(fence, "fence");
    }
}
