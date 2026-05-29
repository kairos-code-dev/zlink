/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.messaging.Received;
@FunctionalInterface
interface RequestReplyCallback {
    void onComplete(Throwable error, Received reply);
}
