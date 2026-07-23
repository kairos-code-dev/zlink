package systems.zlink.framework.locations;

import java.util.Objects;

public record ZLinkRelocationCapacityReserved(
    ZLinkRelocationCapacityFence fence)
    implements ZLinkRelocationCapacityReserveResult {
    public ZLinkRelocationCapacityReserved {
        Objects.requireNonNull(fence, "fence");
    }
}
