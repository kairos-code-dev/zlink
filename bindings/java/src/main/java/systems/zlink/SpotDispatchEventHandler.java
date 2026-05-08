/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

@FunctionalInterface
public interface SpotDispatchEventHandler {
    void onEvent(SpotDispatchInfo info);
}
