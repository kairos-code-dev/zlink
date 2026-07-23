package systems.zlink.framework.locations;

import java.util.Objects;

public record ZLinkRelocationCapacityAlreadyReserved(
    ZLinkRelocationCapacityFence fence)
    implements ZLinkRelocationCapacityReserveResult {
    public ZLinkRelocationCapacityAlreadyReserved {
        Objects.requireNonNull(fence, "fence");
    }
}
