/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


@FunctionalInterface
interface SocketMessageHandler {
    void onMessage(Received received);
}
