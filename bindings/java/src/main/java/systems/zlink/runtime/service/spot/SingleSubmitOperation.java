/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import java.util.Objects;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.runtime.nativeapi.MessagePartsBuffer;

abstract class SingleSubmitOperation {
    private final MessagePartsBuffer parts = new MessagePartsBuffer();
    private SendFlags flags = SendFlags.NONE;
    private boolean submitted;

    protected final void ensureNotSubmitted() {
        if (submitted) {
            throw new IllegalStateException("operation already submitted");
        }
    }

    protected final void markSubmitted() {
        ensureNotSubmitted();
        submitted = true;
    }

    protected final void addPart(Message part) {
        ensureNotSubmitted();
        parts.add(Objects.requireNonNull(part, "part"));
    }

    protected final void requireParts() {
        if (parts.isEmpty()) {
            throw new IllegalArgumentException("at least one message required");
        }
    }

    protected final MessagePartsBuffer parts() {
        return parts;
    }

    protected final SendFlags flags() {
        return flags;
    }

    protected final void setFlags(SendFlags value) {
        ensureNotSubmitted();
        flags = Objects.requireNonNull(value, "flags");
    }
}
