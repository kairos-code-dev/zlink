package systems.zlink.framework.runtime.messaging;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.TimeoutException;
import java.util.function.Supplier;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.framework.channels.ZLinkSubmitResult;
import systems.zlink.framework.channels.ZLinkSubmitStatus;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdmissionKey;
import systems.zlink.framework.runtime.backend.ZLinkBackendObject;

public final class ZLinkSubmitResults {
    private ZLinkSubmitResults() {
    }

    public static CompletionStage<ZLinkSubmitResult> submitAsync(
        ZLinkBackendObject backend,
        ZLinkBackendAdmissionKey key,
        Supplier<Boolean> submission,
        Runnable cleanup) {
        return ZLinkAdmissionRuntime.submit(
            backend, key, submission, cleanup);
    }

    public static CompletionStage<ZLinkSubmitResult> fromVoidStage(
        CompletionStage<Void> submission) {
        CompletableFuture<ZLinkSubmitResult> result = new CompletableFuture<>();
        submission.whenComplete((ignored, error) -> {
            if (error == null) {
                result.complete(result(ZLinkSubmitStatus.SUBMITTED));
                return;
            }
            Throwable cause = unwrap(error);
            ZLinkSubmitResult mapped = fromFailure(cause);
            if (mapped != null) {
                result.complete(mapped);
            } else {
                result.completeExceptionally(cause);
            }
        });
        result.whenComplete((ignored, error) -> {
            if (result.isCancelled()) {
                submission.toCompletableFuture().cancel(false);
            }
        });
        return result;
    }

    public static ZLinkSubmitResult result(ZLinkSubmitStatus status) {
        return new ZLinkSubmitResult(status);
    }

    static ZLinkSubmitResult fromFailure(Throwable error) {
        Throwable cause = unwrap(error);
        if (cause instanceof TimeoutException) {
            return result(ZLinkSubmitStatus.TIMED_OUT);
        }
        if (!(cause instanceof ZlinkSubmitException submit)) {
            return null;
        }
        SubmitResult nativeResult = submit.getResult();
        return switch (nativeResult) {
            case BACKPRESSURED, NOT_ADMITTED -> result(ZLinkSubmitStatus.BACKPRESSURED);
            case NOT_CONNECTED -> result(ZLinkSubmitStatus.ROUTE_NOT_CONNECTED);
            case NOT_FOUND -> result(ZLinkSubmitStatus.TARGET_NOT_FOUND);
            case TERMINATED -> result(ZLinkSubmitStatus.SHUTDOWN);
            default -> null;
        };
    }

    private static Throwable unwrap(Throwable error) {
        Throwable current = error;
        while ((current instanceof CompletionException
                || current instanceof java.util.concurrent.ExecutionException)
            && current.getCause() != null) {
            current = current.getCause();
        }
        return current;
    }
}
