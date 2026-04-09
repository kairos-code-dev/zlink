/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

@FunctionalInterface
public interface RequestReplyCallback {
    void onComplete(Throwable error, Received reply);
}
