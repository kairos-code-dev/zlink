namespace Zlink.Framework.Contracts.Configuration;

/// <summary>
/// Public DI singleton for RouteMesh·MeshNode runtime option changes
/// (spec server/languages/dotnet/05-route-mesh §5). Looking up an
/// unregistered mesh or membership throws <see cref="ZLinkConfigurationException"/>.
/// Only <see cref="IZLinkMeshNodeRuntimeOptions.MaxMessageSize"/> and
/// <see cref="IZLinkMeshChannelRuntimeOptions.Weight"/> may change while the node
/// is serving; high-water marks and timeouts are startup-only through the mesh
/// node's <c>ConfigureRouterSocket()</c>.
/// </summary>
public interface IZLinkRouteMeshRuntimeOptions
{
    IZLinkMeshNodeRuntimeOptions MeshNode(string meshName);

    IZLinkMeshChannelRuntimeOptions Channel(string meshName, string channelName);
}

/// <summary>
/// Runtime MeshNode options. <see cref="MaxMessageSize"/> is a live change.
/// Zero means the framework applies no limit (mapped to Core
/// <c>ZLINK_OPT_MAXMSGSIZE = -1</c>); a positive value is the same byte cap and a
/// negative value is rejected with <see cref="ZLinkConfigurationException"/>.
/// </summary>
public interface IZLinkMeshNodeRuntimeOptions
{
    long MaxMessageSize { get; set; }
}

/// <summary>
/// Runtime channel membership options. Setting <see cref="Weight"/> applies live
/// through <c>IMeshNode.SetChannelWeight</c>. The value ranges from 0 to 100; zero
/// removes the channel from new select-one and Logical Multicast remote targets.
/// </summary>
public interface IZLinkMeshChannelRuntimeOptions
{
    int Weight { get; set; }
}
