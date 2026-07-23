package systems.zlink.framework.locations;

import java.time.Duration;
import java.util.concurrent.CompletionStage;

/**
 * Stores immutable relocation roots before Location authority publishes them.
 */
public interface ZLinkRelocationStore {
    CompletionStage<ZLinkRelocationStored> put(
        byte[] payload,
        Duration retention,
        ZLinkStoreCancellation cancellation);

    CompletionStage<ZLinkRelocationReadResult> get(
        String reference,
        ZLinkStoreCancellation cancellation);

    CompletionStage<ZLinkRelocationRenewResult> renew(
        String reference,
        Duration retention,
        ZLinkStoreCancellation cancellation);

    CompletionStage<ZLinkRelocationDeleteResult> delete(
        String reference,
        ZLinkStoreCancellation cancellation);
}
