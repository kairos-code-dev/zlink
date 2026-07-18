/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/**
 * The outcome of {@link Claim#recvBatch}.
 *
 * @param resultCode the underlying recv result code (0 == OK)
 * @param required the batch capacity the claim requires, when insufficient
 */
public record ClaimRecvResult(int resultCode, ReceiveRequirements required) {
}
