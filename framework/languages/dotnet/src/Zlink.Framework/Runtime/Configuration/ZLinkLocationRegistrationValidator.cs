namespace Zlink.Framework.Runtime.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    private static void ValidateLocations(ZLinkFrameworkRegistration registration)
    {
        var locations = registration.Locations;
        if (locations.StoreInstance is not null && locations.UseInMemoryStores)
        {
            throw new ZLinkConfigurationException(
                "AddLocationStore registers every store role at once and cannot be combined with "
                + "UseInMemoryLocationStores.");
        }

        foreach (var (spotMeshName, routeChannelName) in locations.Options.SpotRouterChannels)
        {
            if (!registration.SpotMeshChannels.ContainsKey(spotMeshName))
                throw new ZLinkConfigurationException(
                    $"Location mapping references unknown Spot mesh '{spotMeshName}'.");

            if (!registration.RouteChannels.ContainsKey(routeChannelName))
                throw new ZLinkConfigurationException(
                    $"Spot mesh '{spotMeshName}' maps to unknown route channel '{routeChannelName}'.");
        }
    }
}
