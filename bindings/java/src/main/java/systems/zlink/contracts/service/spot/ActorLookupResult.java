/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.sockets.RequestResult;
public record ActorLookupResult(RequestResult result,
                                ActorRef actor,
                                int flags) {
}
