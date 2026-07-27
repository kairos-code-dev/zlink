namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkMonitoringSourceValidator(
    ZLinkMonitoringRegistration registration)
{
    public void ValidateRequiredRuntimes(
        ZLinkFrameworkRuntime? frameworkRuntime,
        IZLinkLocationRuntimeQuery? locationQuery)
    {
        if (frameworkRuntime is null && registration.SocketSources.Count > 0)
            throw new ZLinkConfigurationException(
                "Monitoring socket sources require AddZLinkFramework(...).");

        if (locationQuery is null && registration.HasLocationSources)
            throw new ZLinkConfigurationException(
                "Monitoring location sources require location stores registered through AddZLinkFramework(...).");
    }

    public void PreflightFrameworkSources(ZLinkFrameworkRuntime? frameworkRuntime)
    {
        if (frameworkRuntime is null) return;

        foreach (var source in registration.SocketSources.Values)
        {
            var (channelName, capability) = ParseChannelCapabilitySource(source.SourceName);
            if (!frameworkRuntime.Registration.Channels.TryGetValue(channelName, out var channel)
                || !HasCapability(channel, capability))
            {
                throw new ZLinkConfigurationException(
                    $"Socket monitoring source '{source.SourceName}' is not registered.");
            }
        }

        foreach (var meshName in registration.MeshNodeSources)
            if (!frameworkRuntime.Registration.SpotNodes.ContainsKey(meshName))
                throw new ZLinkConfigurationException(
                    $"Mesh monitoring source '{meshName}' is not a registered RouteMesh.");
    }

    private static bool HasCapability(
        ZLinkChannelRegistration channel,
        string capability)
    {
        return capability switch
        {
            "subscriber" => channel.Subscriber is not null,
            "publisher" => channel.Publisher is not null,
            "client" => channel.HasClientServerClient,
            "server" => channel.HasClientServerServer,
            _ => false
        };
    }

    private static (string ChannelName, string Capability) ParseChannelCapabilitySource(string sourceName)
    {
        var separatorIndex = sourceName.LastIndexOf('.');
        if (separatorIndex <= 0 || separatorIndex == sourceName.Length - 1)
            throw new ZLinkConfigurationException(
                $"Socket monitoring source '{sourceName}' must use '<channel>.<capability>'.");

        return (sourceName[..separatorIndex], sourceName[(separatorIndex + 1)..]);
    }
}
