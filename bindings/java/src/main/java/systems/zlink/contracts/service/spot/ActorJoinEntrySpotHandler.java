/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** Callback for {@link ActorJoinEntrySpotOperation#submit(ActorJoinEntrySpotHandler)}. */
@FunctionalInterface
public interface ActorJoinEntrySpotHandler {
    void onJoinEntrySpotResult(ActorJoinEntrySpotResult result);
}
