/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

@FunctionalInterface
interface RequestReplyCallback {
    void onComplete(Throwable error, Received reply);
}
