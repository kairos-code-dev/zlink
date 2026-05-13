/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.RequestResult;

public record ActorLookupResult(RequestResult result,
                                ActorRef actor,
                                int flags) {
}
