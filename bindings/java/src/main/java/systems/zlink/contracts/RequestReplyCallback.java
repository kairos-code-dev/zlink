/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


@FunctionalInterface
interface RequestReplyCallback {
    void onComplete(Throwable error, Received reply);
}
