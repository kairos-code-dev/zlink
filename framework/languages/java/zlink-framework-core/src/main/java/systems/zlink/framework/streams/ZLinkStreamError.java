package systems.zlink.framework.streams;

import java.util.Optional;

public record ZLinkStreamError(
    ZLinkStreamSessionError error,
    Optional<ZLinkStreamDiagnostic> diagnostic) {
}
