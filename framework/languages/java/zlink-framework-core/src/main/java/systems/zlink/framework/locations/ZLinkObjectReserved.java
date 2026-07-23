package systems.zlink.framework.locations;

import java.util.Objects;

public record ZLinkObjectReserved(ZLinkObjectReservation reservation)
    implements ZLinkObjectReserveResult {
    public ZLinkObjectReserved {
        Objects.requireNonNull(reservation, "reservation");
    }
}
