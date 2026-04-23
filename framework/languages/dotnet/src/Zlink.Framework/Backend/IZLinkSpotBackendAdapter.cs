namespace Zlink.Framework.Backend;

internal interface IZLinkSpotBackendAdapter
{
    IZLinkBackendSpotNode CreateSpotNode(IZLinkBackendContext context);
}
