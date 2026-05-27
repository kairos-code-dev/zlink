/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;


@FunctionalInterface
public interface SocketMonitorHandler {
    void onEvent(MonitorEvent event);
}
