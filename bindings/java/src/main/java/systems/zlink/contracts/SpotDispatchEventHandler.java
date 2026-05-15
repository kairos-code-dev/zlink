/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


@FunctionalInterface
public interface SpotDispatchEventHandler {
    void onEvent(SpotDispatchInfo info);
}
