/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Received;

/** Handles a message received by {@link ZlinkNativeDispatcher}. */
@FunctionalInterface
public interface ZlinkReceiveHandler {
    CompletionStage<Void> handle(Received message);
}
