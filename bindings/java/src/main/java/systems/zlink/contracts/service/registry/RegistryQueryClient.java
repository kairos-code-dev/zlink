/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

import java.util.List;

public interface RegistryQueryClient extends AutoCloseable {

    public abstract void connect(String endpoint);

    public abstract List<RegistryTopologyEntry> topology();

    public abstract List<RegistryTopologyEntry> topology(
      RegistryTopologyFilter filter);

    @Override
    public abstract void close();
}
