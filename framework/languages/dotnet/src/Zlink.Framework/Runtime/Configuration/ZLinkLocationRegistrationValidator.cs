namespace Zlink.Framework.Runtime.Configuration;

internal static partial class ZLinkFrameworkRegistrationValidator
{
    private static void ValidateLocations(ZLinkFrameworkRegistration registration)
    {
        var locations = registration.Locations;
        if (locations.UseInMemoryStores && locations.HasAnyStoreType)
        {
            throw new ZLinkConfigurationException(
                "In-memory location stores cannot be combined with explicit location store registrations.");
        }

        if (locations.HasAnyStoreType && !locations.HasAllStoreTypes)
        {
            throw new ZLinkConfigurationException(
                "Location stores are all-or-nothing: register peer, spot, actor, route, and owner lease "
                + "stores together. The five roles must share one physical store.");
        }
    }
}
