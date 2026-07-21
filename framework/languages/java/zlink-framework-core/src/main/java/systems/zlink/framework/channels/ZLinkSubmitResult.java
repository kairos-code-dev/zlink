package systems.zlink.framework.channels;

import java.util.Objects;

public record ZLinkSubmitResult(ZLinkSubmitStatus status) {
    public ZLinkSubmitResult {
        Objects.requireNonNull(status, "status");
    }
}
