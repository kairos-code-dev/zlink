/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;


/**
 * Actor lifecycle callback registered through
 * {@link Spot#onActorLifecycle(ActorLifecycleHandler, ActorLifecycleHandler)}.
 *
 * The supplied {@link SpotActorLifecycleInfo} is valid only for the duration
 * of the callback. Applications must copy fields they need to retain.
 */
@FunctionalInterface
public interface ActorLifecycleHandler {
    void onLifecycleEvent(Spot spot, SpotActorLifecycleInfo info);
}
