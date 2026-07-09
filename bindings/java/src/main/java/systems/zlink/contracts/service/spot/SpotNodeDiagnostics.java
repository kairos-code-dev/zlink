/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import java.util.List;

/** Snapshot and diagnostic views exposed by a spot node. */
public interface SpotNodeDiagnostics {
    /** Returns the current node status snapshot. */
    SpotNodeStatus status();

    /** Returns the current peer snapshot. */
    List<SpotNodePeerEntry> peers();

    /** Returns peer entries matching the supplied filter. */
    List<SpotNodePeerEntry> peers(
      SpotNodePeerFilter filter);

    /** Returns the current subject snapshot. */
    List<SpotNodeSubjectEntry> subjects();

    /** Returns subject entries matching the supplied filter. */
    List<SpotNodeSubjectEntry> subjects(
      SpotNodeSubjectFilter filter);

    /** Returns diagnostic socket snapshot rows that exist on this node. */
    List<SpotNodeSocketEntry> internalSockets();

    List<SpotNodeSpotEntry> spots();

    List<SpotNodeActorEntry> actors();

    /** Returns diagnostic socket snapshot rows matching the supplied filter. */
    List<SpotNodeSocketEntry> internalSockets(
      SpotNodeSocketFilter filter);
}
