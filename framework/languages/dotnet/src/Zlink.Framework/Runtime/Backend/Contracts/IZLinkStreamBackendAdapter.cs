namespace Zlink.Framework.Runtime.Backend.Contracts;

internal interface IZLinkStreamBackendAdapter
{
    // A STREAM node that uses Actor dispatch binds sessions to actors on the
    // framework's single MeshNode for its target MeshName (spec 31 §2), so the
    // session service is created from that shared node rather than a freshly
    // minted one. actorDispatchNode is that shared node (null when the process
    // has no MeshNode, in which case a standalone node is minted as a fallback).
    // standaloneMeshName names the fallback MeshNode minted when actorDispatchNode
    // is null; Core requires a non-empty mesh name at construction (EINVAL otherwise).
    IZLinkBackendStreamSocket CreateStreamSocket(
        IZLinkBackendContext context,
        string standaloneMeshName,
        IZLinkBackendSpotNode? actorDispatchNode = null);
}