/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

import java.util.List;

/**
 * A read-only client that queries a registry for its topology.
 * The caller owns this resource and must close it.
 */
public interface RegistryQueryClient extends AutoCloseable {

    void connect(String endpoint);

    List<RegistryTopologyEntry> topology();

    List<RegistryTopologyEntry> topology(
      RegistryTopologyFilter filter);

    @Override
    void close();
}
