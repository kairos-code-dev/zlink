// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     A spot node: hosts spots and actors, tunes their sockets, and exposes the
///     node's peers, subjects, and topology.
/// </summary>
public partial interface ISpotNode : ISpotNodeConfiguration,
    ISpotNodePeers,
    ISpotNodeSpots,
    ISpotNodeActors,
    ISpotNodeTopology,
    IDisposable,
    IAsyncDisposable
{
    /// <summary>
    ///     Closes the SPOT node and releases its native resources.
    /// </summary>
    void Close();
}
