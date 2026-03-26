/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

@FunctionalInterface
public interface ServiceMonitorHandler {
    void onEvent(ServiceEvent event);
}
