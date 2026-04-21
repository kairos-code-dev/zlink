/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

@FunctionalInterface
interface SocketMessageHandler {
    void onMessage(Received received);
}
