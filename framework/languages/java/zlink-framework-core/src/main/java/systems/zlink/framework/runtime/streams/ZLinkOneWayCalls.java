package systems.zlink.framework.runtime.streams;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.BiFunction;
import java.util.function.Supplier;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendAdmissionKey;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendObject;

final class ZLinkOneWayCalls {
    static final int SUBMITTED = 0;
    static final int BACKPRESSURED = 1;
    static final int TIMED_OUT = 2;
    static final int ROUTE_NOT_CONNECTED = 3;
    static final int TARGET_NOT_FOUND = 4;
    static final int SHUTDOWN = 5;

    private final BiFunction<
        ZLinkBackendObject,
        ZLinkBackendAdmissionKey,
        BiFunction<Supplier<Boolean>, Runnable, CompletionStage<Void>>> admission;

    ZLinkOneWayCalls(BiFunction<
        ZLinkBackendObject,
        ZLinkBackendAdmissionKey,
        BiFunction<Supplier<Boolean>, Runnable, CompletionStage<Void>>> admission) {
        this.admission = java.util.Objects.requireNonNull(admission, "admission");
    }

    CompletionStage<Void> submitOneWay(
        ZLinkBackendObject backend,
        ZLinkBackendAdmissionKey key,
        Supplier<Boolean> submission,
        Runnable cleanup) {
        return admission.apply(backend, key).apply(submission, cleanup);
    }

    static <T> CompletionStage<T> beginOneWay(AtomicBoolean submitted) {
        if (submitted.compareAndSet(false, true)) {
            return null;
        }
        return CompletableFuture.failedFuture(new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ALREADY_SUBMITTED,
            "call has already been submitted"));
    }

    static CompletionStage<Void> oneWayStatus(int status) {
        RuntimeException failure = switch (status) {
            case SUBMITTED -> null;
            case TIMED_OUT, BACKPRESSURED -> new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.DEADLINE_EXCEEDED,
                "one-way submission did not obtain queue capacity before the send deadline");
            case ROUTE_NOT_CONNECTED -> new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ROUTE_NOT_CONNECTED,
                "one-way route is not connected");
            case TARGET_NOT_FOUND -> new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.REQUEST_TARGET_NOT_FOUND,
                "one-way target was not found");
            case SHUTDOWN -> new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RUNTIME_SHUTDOWN,
                "framework runtime is shutting down");
            default -> throw new IllegalArgumentException(
                "unknown one-way admission status: " + status);
        };
        return failure == null
            ? CompletableFuture.completedFuture(null)
            : CompletableFuture.failedFuture(failure);
    }

    static CompletionStage<Void> adaptOneWay(CompletionStage<Void> submission) {
        CompletableFuture<Void> result = new CompletableFuture<>();
        submission.whenComplete((ignored, error) -> {
            if (error == null) {
                result.complete(null);
                return;
            }
            Throwable cause = unwrap(error);
            if (cause instanceof ZlinkSubmitException submit) {
                CompletionStage<Void> mapped = switch (submit.getResult()) {
                    case BACKPRESSURED, NOT_ADMITTED -> oneWayStatus(BACKPRESSURED);
                    case NOT_CONNECTED -> oneWayStatus(ROUTE_NOT_CONNECTED);
                    case NOT_FOUND -> oneWayStatus(TARGET_NOT_FOUND);
                    case TERMINATED -> oneWayStatus(SHUTDOWN);
                    default -> null;
                };
                if (mapped != null) {
                    mapped.whenComplete((unused, mappedError) ->
                        result.completeExceptionally(unwrap(mappedError)));
                    return;
                }
            }
            result.completeExceptionally(cause);
        });
        return result;
    }

    private static Throwable unwrap(Throwable error) {
        Throwable current = error;
        while (current instanceof CompletionException && current.getCause() != null) {
            current = current.getCause();
        }
        return current;
    }
}
