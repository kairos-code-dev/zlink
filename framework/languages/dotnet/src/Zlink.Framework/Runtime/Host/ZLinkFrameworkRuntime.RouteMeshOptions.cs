using Zlink.Framework.Runtime.Channels;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.Runtime.Host;

// RouteMesh·MeshNode runtime-option resolution (spec 05-route-mesh §5, S8-02A).
// A "mesh" is the process-local MeshNode registered under its MeshName; in the
// current surface that is the spot node keyed by SpotNodeName. Weight resolution
// reaches the live IMeshNode through ZLinkSpotNodeRuntime.Node.
internal sealed partial class ZLinkFrameworkRuntime
{
    internal IZLinkMeshNodeRuntimeOptions ResolveMeshNodeRuntimeOptions(string meshName)
    {
        return ExecuteOperation<IZLinkMeshNodeRuntimeOptions>(() =>
        {
            var (_, registration) = ResolveMeshNode(meshName);
            return new ZLinkMeshNodeRuntimeOptions(registration);
        });
    }

    internal IZLinkMeshChannelRuntimeOptions ResolveMeshChannelRuntimeOptions(
        string meshName,
        string channelName)
    {
        return ExecuteOperation<IZLinkMeshChannelRuntimeOptions>(() =>
        {
            var (nodeRuntime, registration) = ResolveMeshNode(meshName);
            var meshChannelName = registration.SpotMeshChannelName ?? registration.SpotNodeName;
            if (!string.Equals(channelName, meshChannelName, StringComparison.Ordinal))
                throw new ZLinkConfigurationException(
                    $"RouteMesh '{meshName}' has no channel membership '{channelName}'.");
            if (registration.Router is not { } router)
                throw new ZLinkConfigurationException(
                    $"RouteMesh '{meshName}' has no serving channel on this node.");

            return new ZLinkMeshChannelRuntimeOptions(
                this, nodeRuntime.Node, meshChannelName, router.SocketConfig);
        });
    }

    private (ZLinkSpotNodeRuntime NodeRuntime, ZLinkSpotNodeRegistration Registration) ResolveMeshNode(
        string meshName)
    {
        if (!Registration.SpotNodes.TryGetValue(meshName, out var registration))
            throw new ZLinkConfigurationException(
                $"RouteMesh '{meshName}' is not registered on this node.");

        var state = GetOrStartState();
        if (!state.SpotNodes.TryGetValue(meshName, out var nodeRuntime))
            throw new ZLinkConfigurationException(
                $"RouteMesh '{meshName}' is not running on this node.");

        return (nodeRuntime, registration);
    }
}
