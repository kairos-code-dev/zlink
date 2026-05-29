/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

public record RegistryStatus(int registryId, String bindEndpoint, RegistryState state,
                             int topologyEntryCount, int peerRegistryCount,
                             int connectedPeerRegistryCount, long listSeq,
                             int lastError, long lastChangedMs) {
}
