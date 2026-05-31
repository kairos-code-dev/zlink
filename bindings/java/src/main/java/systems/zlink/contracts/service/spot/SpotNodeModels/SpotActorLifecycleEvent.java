/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/**
 * An actor lifecycle event delivered to a lifecycle subscriber.
 * @param kind the kind of lifecycle event
 * @param info details of the lifecycle change
 */
public record SpotActorLifecycleEvent(SpotActorLifecycleEventKind kind,
                                      SpotActorLifecycleInfo info) {
}
