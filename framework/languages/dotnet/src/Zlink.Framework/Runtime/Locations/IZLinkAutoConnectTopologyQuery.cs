namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Send-path view of the auto-connect state: is this node rid a peer the
/// mesh knows? Answers come from the reconciler's desired-set snapshot and
/// never from the store, so the send path stays free of hidden store I/O
/// (spot-address messaging contract §7). Null means no judgment is possible —
/// no loop manages the mesh, or the first reconcile has not completed.
/// </summary>
internal interface IZLinkAutoConnectTopologyQuery
{
    bool? IsKnownRouteMeshPeer(string meshName, RoutingId nodeRid);
}
