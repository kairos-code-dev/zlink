package systems.zlink.e2e.registrationcodec;

import java.util.Optional;

final class FilterOrderValues {
    private FilterOrderValues() {
    }

    static Optional<String> from(Object request) {
        if (request instanceof Contracts.EchoManualRequest manual
            && manual.value().startsWith("filter-order")) {
            return Optional.of(manual.value());
        }
        return Optional.empty();
    }
}
