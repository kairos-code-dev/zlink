/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


@FunctionalInterface
public interface SpotRoutedHandler {
    void onMessage(RoutingId sourceRid, RoutingId spotRid, long requestSeq,
                   Received received);
}
