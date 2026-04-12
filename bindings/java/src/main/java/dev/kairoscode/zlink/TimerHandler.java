/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

@FunctionalInterface
public interface TimerHandler {
    void onFire(Timer timer, long fireCount);
}
