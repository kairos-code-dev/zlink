namespace Zlink.Framework.Runtime.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    public static void Validate(ZLinkFrameworkRegistration registration)
    {
        var globalSpotFactories = new HashSet<string>(StringComparer.Ordinal);
        var globalSpotPublisherChannels = new HashSet<string>(StringComparer.Ordinal);

        if (registration.SpotDiscovery is { RequiresUseDiscovery: true, UseDiscoveryCalled: false })
        {
            throw new ZLinkConfigurationException(
                "AddSpotMesh(...) requires spotMesh.UseDiscovery(...).");
        }

        foreach (var channel in registration.Channels.Values)
        {
            ValidateChannel(channel, registration.Discovery is not null);
        }

        foreach (var streamNode in registration.StreamNodes.Values)
        {
            ValidateStreamNode(streamNode);
        }

        foreach (var routed in registration.RouteChannels.Values)
        {
            ValidateRouteChannel(routed, registration.Discovery is not null);
        }

        foreach (var spotNode in registration.SpotNodes.Values)
        {
            ValidateSpotNode(
                spotNode,
                registration,
                globalSpotFactories,
                globalSpotPublisherChannels);
        }
    }

    private static void ValidateStreamNode(ZLinkStreamNodeRegistration streamNode)
    {
        if (string.IsNullOrWhiteSpace(streamNode.BindEndpoint))
        {
            throw new ZLinkConfigurationException(
                $"STREAM node '{streamNode.StreamNodeName}' must define a bind endpoint.");
        }

        if (streamNode.HeaderSessionType is null)
        {
            throw new ZLinkConfigurationException(
                $"STREAM node '{streamNode.StreamNodeName}' must register a header stream session.");
        }
    }

}
