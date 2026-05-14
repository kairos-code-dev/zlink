namespace Zlink.Framework.Runtime.Configuration.Builders;

internal sealed class ZLinkDiscoveryBuilder(List<string> endpoints) : IZLinkDiscoveryBuilder
{
    public void Add(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("Discovery endpoint must not be empty.");
        }

        endpoints.Add(endpoint);
    }
}
