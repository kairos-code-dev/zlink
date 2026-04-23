namespace Zlink.Framework.Backend;

internal interface IZLinkRegistryBackendAdapter
{
    IZLinkBackendRegistry CreateRegistry(IZLinkBackendContext context);

    IZLinkBackendRegistryQueryClient CreateRegistryQueryClient(IZLinkBackendContext context);
}
