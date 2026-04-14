/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.service.registry.ServiceEvent;

@FunctionalInterface
public interface ServiceMonitorHandler {
    void onEvent(ServiceEvent event);
}
