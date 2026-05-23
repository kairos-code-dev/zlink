namespace Zlink.Framework.Runtime.Configuration.Builders;

internal sealed class ZLinkStreamNodeBuilder(ZLinkStreamNodeRegistration registration) : IZLinkStreamNodeBuilder
{
    public void Bind(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
        {
            throw new ZLinkConfigurationException("STREAM bind endpoint must not be empty.");
        }

        registration.BindEndpoint = endpoint;
    }

    public void AttachActorGateway(string spotNodeName)
    {
        if (string.IsNullOrWhiteSpace(spotNodeName))
        {
            throw new ZLinkConfigurationException("STREAM ActorGateway target SpotNode name must not be empty.");
        }

        registration.ActorGatewaySpotNodeName = spotNodeName;
    }

    public void RegisterSession<TSession>()
        where TSession : class, IZLinkSession
    {
        if (registration.HeaderSessionType is not null)
        {
            throw new ZLinkConfigurationException(
                $"STREAM node '{registration.StreamNodeName}' already has a stream session.");
        }

        registration.HeaderSessionType = typeof(TSession);
    }
}
