package systems.zlink.framework.runtime.internal.streams;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.util.Locale;
import java.util.Map;
import systems.zlink.framework.errors.ZLinkFrameworkException;

/** Encodes the common JSON body carried by a STREAM Error frame. */
public final class ZLinkStreamErrorPayload {
    private static final ObjectMapper MAPPER = new ObjectMapper();

    private ZLinkStreamErrorPayload() {
    }

    public static byte[] encode(Throwable failure) {
        Throwable error = unwrap(failure);
        String code = error instanceof ZLinkFrameworkException frameworkError
            ? frameworkError.kind().name().toLowerCase(Locale.ROOT)
            : error.getClass().getSimpleName();
        String message = error.getMessage();
        if (message == null || message.isBlank()) {
            message = error.getClass().getSimpleName();
        }
        try {
            return MAPPER.writeValueAsBytes(Map.of(
                "code", code,
                "message", message));
        } catch (JsonProcessingException encodingFailure) {
            throw new IllegalStateException(
                "failed to encode STREAM error payload", encodingFailure);
        }
    }

    private static Throwable unwrap(Throwable failure) {
        Throwable current = failure == null
            ? new IllegalStateException("handler failed")
            : failure;
        while ((current instanceof java.util.concurrent.CompletionException
                || current instanceof java.util.concurrent.ExecutionException)
            && current.getCause() != null) {
            current = current.getCause();
        }
        return current;
    }
}
