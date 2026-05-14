namespace Zlink.Framework.Runtime.Backend.Contracts;

internal interface IZLinkRegistryBackendAdapter
{
    IZLinkBackendRegistry CreateRegistry(IZLinkBackendContext context);

    IZLinkBackendRegistryQueryClient CreateRegistryQueryClient(IZLinkBackendContext context);
}
