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

    public IZLinkStreamNodeBuilder EnableActorDispatch(string meshName)
    {
        if (string.IsNullOrWhiteSpace(meshName))
            throw new ZLinkConfigurationException("Actor dispatch MeshName must not be empty.");

        if (registration.ActorDispatchMeshName is not null)
            throw new ZLinkConfigurationException(
                $"STREAM node '{registration.StreamNodeName}' already enabled actor dispatch for "
                + $"MeshName '{registration.ActorDispatchMeshName}'.");

        registration.ActorDispatchMeshName = meshName;
        return this;
    }

    public IZLinkStreamNodeBuilder SetTlsServer(
        string certificatePath,
        string keyPath,
        bool requireClientCertificate = false)
    {
        if (string.IsNullOrWhiteSpace(certificatePath))
            throw new ZLinkConfigurationException("STREAM TLS certificate path must not be empty.");

        if (string.IsNullOrWhiteSpace(keyPath))
            throw new ZLinkConfigurationException("STREAM TLS key path must not be empty.");

        registration.TlsServer = new ZLinkStreamTlsServerRegistration(
            certificatePath,
            keyPath,
            requireClientCertificate);
        return this;
    }

    public IZLinkStreamNodeBuilder AddSession<TSession>()
        where TSession : class, IZLinkSession
    {
        if (registration.HeaderSessionType is not null)
            throw new ZLinkConfigurationException(
                $"STREAM node '{registration.StreamNodeName}' already has a stream session.");

        registration.HeaderSessionType = typeof(TSession);
        return this;
    }
}
