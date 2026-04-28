namespace Zlink.Framework.Configuration.Builders;

internal sealed class ZLinkMutableConnections(List<string> endpoints)
    : IChannelClientConnections,
      IChannelSubscriberConnections,
      ISpotRouterConnections,
      ISpotPubSubConnections,
      ISpotPublisherConnections
{
    public void Connect(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("Connection endpoint must not be empty.");
        }

        endpoints.Add(endpoint);
    }

    public void Disconnect(string endpoint)
    {
        endpoints.Remove(endpoint);
    }

    public IReadOnlyList<string> ListConnections()
    {
        return endpoints.AsReadOnly();
    }
}
