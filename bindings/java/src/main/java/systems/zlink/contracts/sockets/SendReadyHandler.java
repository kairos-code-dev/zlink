/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;


@FunctionalInterface
public interface SendReadyHandler {
    void onReady();
}
