/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.MonitorEvent;
import dev.kairoscode.zlink.MonitorEventType;

public record SpotServiceMonitorEvent(String serviceName,
                                      SpotServiceAttachmentRole role,
                                      MonitorEvent event) {
    public MonitorEventType eventType() {
        return event.event();
    }
}
