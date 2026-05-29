/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;


/** Callback for {@link ActorLookupOperation#submit(ActorLookupHandler)}. */
@FunctionalInterface
public interface ActorLookupHandler {
    void onLookupResult(ActorLookupResult result);
}
