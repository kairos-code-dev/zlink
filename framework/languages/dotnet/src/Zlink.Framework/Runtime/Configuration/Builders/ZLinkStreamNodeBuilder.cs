namespace Zlink.Framework.Runtime.Configuration.Builders;

internal sealed class ZLinkStreamNodeBuilder(ZLinkStreamNodeRegistration registration) : IZLinkStreamNodeBuilder
{
    public IZLinkStreamNodeBuilder Bind(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
            throw new ZLinkConfigurationException("STREAM bind endpoint must not be empty.");

        registration.BindEndpoint = endpoint;
        return this;
    }

    public IZLinkStreamNodeBuilder SetTlsServer(string certPath, string keyPath, bool requireClientCert = false)
    {
        if (string.IsNullOrWhiteSpace(certPath))
            throw new ZLinkConfigurationException("STREAM TLS certificate path must not be empty.");

        if (string.IsNullOrWhiteSpace(keyPath))
            throw new ZLinkConfigurationException("STREAM TLS key path must not be empty.");

        registration.TlsServer = new ZLinkStreamTlsServerRegistration(certPath, keyPath, requireClientCert);
        return this;
    }

    public IZLinkStreamNodeBuilder RegisterSession<TSession>()
        where TSession : class, IZLinkSession
    {
        if (registration.HeaderSessionType is not null)
            throw new ZLinkConfigurationException(
                $"STREAM node '{registration.StreamNodeName}' already has a stream session.");

        registration.HeaderSessionType = typeof(TSession);
        return this;
    }
}