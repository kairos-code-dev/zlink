// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     Reads status and topology snapshots from a SPOT node.
/// </summary>
public interface ISpotNodeTopology
{
    /// <summary>
    ///     Gets the current status.
    /// </summary>
    SpotNodeStatus Status();

    /// <summary>
    ///     Gets peer entries.
    /// </summary>
    SpotNodePeerEntry[] Peers();

    /// <summary>
    ///     Gets peer entries matching a filter.
    /// </summary>
    SpotNodePeerEntry[] PeersQuery(SpotNodePeerFilter filter);

    /// <summary>
    ///     Gets subject entries matching a filter.
    /// </summary>
    SpotNodeSubjectEntry[] Subjects(
        SpotNodeSubjectFilter? filter = null);

    /// <summary>
    ///     Gets internal socket entries matching a filter.
    /// </summary>
    SpotNodeSocketEntry[] InternalSockets(
        SpotNodeSocketFilter? filter = null);

    /// <summary>
    ///     Gets spot entries.
    /// </summary>
    SpotNodeSpotEntry[] Spots();

    /// <summary>
    ///     Gets actor entries.
    /// </summary>
    SpotNodeActorEntry[] Actors();
}
