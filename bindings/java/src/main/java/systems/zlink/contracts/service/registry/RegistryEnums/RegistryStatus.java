/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

/**
 * A snapshot of a registry's status.
 * @param registryId the registry identifier
 * @param bindEndpoint the registry's bound endpoint
 * @param state the current operational state
 * @param topologyEntryCount the number of topology entries
 * @param peerRegistryCount the number of peer registries
 * @param connectedPeerRegistryCount the number of connected peer registries
 * @param listSeq the topology list sequence number
 * @param lastError the last native error code, or 0 for none
 * @param lastChangedMs when the status last changed, in milliseconds
 */
public record RegistryStatus(int registryId, String bindEndpoint, RegistryState state,
                             int topologyEntryCount, int peerRegistryCount,
                             int connectedPeerRegistryCount, long listSeq,
                             int lastError, long lastChangedMs) {
}
