/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.sockets.RequestResult;
/**
 * The outcome of an actor lookup.
 * @param result the request result
 * @param actor the actor reference; valid on success
 * @param flags operation flags
 */
public record ActorLookupResult(RequestResult result,
                                ActorRef actor,
                                int flags) {
}
