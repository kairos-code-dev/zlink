namespace Zlink.Framework.Configuration.Builders;

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

    public void AddHeaderSession<TSession>()
        where TSession : class, IZLinkStreamHeaderSession
    {
        if (registration.HeaderSessionType is not null)
        {
            throw new ZLinkConfigurationException(
                $"STREAM node '{registration.StreamNodeName}' already has a stream session.");
        }

        registration.HeaderSessionType = typeof(TSession);
    }
}
