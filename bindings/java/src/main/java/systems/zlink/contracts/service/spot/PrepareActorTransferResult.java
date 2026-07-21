/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** The token and reservation detail returned by actor transfer prepare. */
public record PrepareActorTransferResult(
    ActorTransferToken token,
    ActorTransferPrepareResult result) {
}
