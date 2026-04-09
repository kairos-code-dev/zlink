/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

@FunctionalInterface
public interface RequestHandler {
    void onRequest(RoutingId routingId, long correlationId, Received received);
}
