/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

@FunctionalInterface
public interface SocketMonitorHandler {
    void onEvent(MonitorEvent event);
}
