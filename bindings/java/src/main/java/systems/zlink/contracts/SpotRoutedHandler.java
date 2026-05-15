/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


@FunctionalInterface
public interface SpotRoutedHandler {
    void onMessage(RoutingId sourceRid, RoutingId spotRid, long requestSeq,
                   Received received);
}
