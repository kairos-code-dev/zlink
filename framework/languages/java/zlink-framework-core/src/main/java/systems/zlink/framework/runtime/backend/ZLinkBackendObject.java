package systems.zlink.framework.runtime.backend;

import java.time.Duration;
import java.util.function.Consumer;

public interface ZLinkBackendObject extends AutoCloseable {
    int DEFAULT_PENDING_ADMISSION_CAPACITY = 4096;

    String name();

    /** Returns the object that owns this object's send-capacity signal. */
    default ZLinkBackendObject admissionSource() {
        return this;
    }

    /** Installs the internal send-capacity callback for this source. */
    default void setAdmissionReadyHandler(
        Consumer<ZLinkBackendAdmissionKey> handler) {
    }

    /** Installs the internal callback that terminates pending admission on close. */
    default void setAdmissionShutdownHandler(Runnable handler) {
    }

    /** Returns the family send admission deadline. */
    default Duration admissionTimeout() {
        return Duration.ofSeconds(1);
    }

    /** Returns the bounded number of one-way admissions waiting for capacity. */
    default int admissionPendingCapacity() {
        return DEFAULT_PENDING_ADMISSION_CAPACITY;
    }

    @Override
    void close();
}
