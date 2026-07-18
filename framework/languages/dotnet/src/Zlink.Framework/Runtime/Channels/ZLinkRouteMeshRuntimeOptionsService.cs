using Zlink.Framework.Runtime.Host;

namespace Zlink.Framework.Runtime.Channels;

// Public DI singleton implementation of IZLinkRouteMeshRuntimeOptions
// (spec server/languages/dotnet/05-route-mesh §5). MeshNode(meshName) and
// Channel(meshName, channelName) resolve against the running MeshNode (spot node)
// registered under meshName; unknown mesh or membership throws
// ZLinkConfigurationException. Weight applies live via IMeshNode.SetChannelWeight.
internal sealed class ZLinkRouteMeshRuntimeOptionsService(ZLinkFrameworkRuntime runtime)
    : IZLinkRouteMeshRuntimeOptions
{
    public IZLinkMeshNodeRuntimeOptions MeshNode(string meshName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(meshName);
        return runtime.ResolveMeshNodeRuntimeOptions(meshName);
    }

    public IZLinkMeshChannelRuntimeOptions Channel(string meshName, string channelName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(meshName);
        ArgumentException.ThrowIfNullOrWhiteSpace(channelName);
        return runtime.ResolveMeshChannelRuntimeOptions(meshName, channelName);
    }
}

// Live MeshNode option surface. MaxMessageSize is validated against the spec
// (negative rejected, zero = no framework limit) and recorded on the node's
// ROUTER socket recipe. The 10.0.0 IMeshNode binding exposes no runtime
// MaxMessageSize setter, so the byte cap is fixed at node startup from the recipe;
// runtime writes update the recipe value the interface reports.
internal sealed class ZLinkMeshNodeRuntimeOptions(ZLinkSpotNodeRegistration registration)
    : IZLinkMeshNodeRuntimeOptions
{
    public long MaxMessageSize
    {
        get => registration.Router?.SocketConfig.MaxMessageSize ?? 0;
        set
        {
            if (value < 0)
                throw new ZLinkConfigurationException(
                    "MaxMessageSize must not be negative; use 0 for no framework limit.");
            if (registration.Router is not { } router)
                throw new ZLinkConfigurationException(
                    $"MeshNode '{registration.SpotNodeName}' has no ROUTER socket to configure MaxMessageSize.");
            router.SocketConfig.MaxMessageSize = value;
        }
    }
}

// Live channel-weight surface. Weight get reflects the last applied value from the
// ROUTER socket recipe; set validates the 0..100 range and applies through
// IMeshNode.SetChannelWeight, which Core turns into a descriptor-revision bump.
internal sealed class ZLinkMeshChannelRuntimeOptions(
    ZLinkFrameworkRuntime runtime,
    IZLinkBackendSpotNode node,
    string channelName,
    ZLinkSocketConfig recipe) : IZLinkMeshChannelRuntimeOptions
{
    public int Weight
    {
        get => recipe.Weight;
        set
        {
            ZLinkSocketConfig.ValidatePeerWeight(value);
            runtime.ExecuteOperation(() =>
            {
                node.SetChannelWeight(channelName, (uint)value);
                recipe.Weight = value;
                return true;
            });
        }
    }
}
