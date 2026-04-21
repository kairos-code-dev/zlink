/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

@FunctionalInterface
public interface SpotDispatchEventHandler {
    void onEvent(SpotDispatchInfo info);
}
