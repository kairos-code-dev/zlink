/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;


/** Callback invoked for each spot dispatch event. */
@FunctionalInterface
public interface SpotDispatchEventHandler {
    void onEvent(SpotDispatchInfo info);
}
