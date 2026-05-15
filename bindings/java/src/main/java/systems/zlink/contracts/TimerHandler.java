/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


@FunctionalInterface
public interface TimerHandler {
    void onFire(Timer timer, long fireCount);
}
