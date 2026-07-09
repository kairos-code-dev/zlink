// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     Creates and looks up SPOT handles owned by a SPOT node.
/// </summary>
public interface ISpotNodeSpots
{
    /// <summary>
    ///     Creates a publisher handle for the local SPOT topic plane.
    /// </summary>
    ISpotNodePublisher CreatePublisher();

    /// <summary>
    ///     Creates a spot hosted by this node.
    /// </summary>
    ISpot CreateSpot();

    /// <summary>
    ///     Gets the entry spot.
    /// </summary>
    ISpot EntrySpot();

    /// <summary>
    ///     Gets an existing spot or creates one for the supplied routing id.
    /// </summary>
    ISpot GetOrCreateSpot(RoutingId spotRid, out bool created);

    /// <summary>
    ///     Looks up a spot by routing id.
    /// </summary>
    ISpot? SpotLookup(RoutingId spotRid);
}
