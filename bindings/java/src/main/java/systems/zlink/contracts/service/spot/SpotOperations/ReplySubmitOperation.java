/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
/** Accepts further parts, flags, and the terminal submit of a reply builder. */
public interface ReplySubmitOperation {
    /**
     * Adds another reply part; consumed on a successful submit (see
     * {@link SendOperation} for the ownership contract).
     *
     * @param part the reply part
     * @return this operation for chaining
     */
    ReplySubmitOperation message(Message part);

    /**
     * Sets the send flags, replacing any previously set flags.
     *
     * @param flags the flags to apply at submit time
     * @return this operation for chaining
     */
    ReplySubmitOperation flags(SendFlags flags);

    /**
     * Submits the reply. Failures throw
     * {@link systems.zlink.contracts.errors.ZlinkException}.
     */
    void submit();
}
