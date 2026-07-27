/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.framework.runtime.internal.binding.spot;

import systems.zlink.contracts.core.RoutingId;

/** A binding between a STREAM session and an actor. */
public record StreamSessionBinding(RoutingId sessionRid, ActorRef actor,
                                   long bindingGeneration, long membershipEpoch) {
}
