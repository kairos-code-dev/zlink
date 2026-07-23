package systems.zlink.framework.locations;

import java.util.Objects;

public record ZLinkRelocationCapacityFence(String value) {
    public ZLinkRelocationCapacityFence {
        Objects.requireNonNull(value, "value");
        if (value.isBlank()) {
            throw new IllegalArgumentException(
                "relocation capacity fence must not be blank");
        }
    }
}
