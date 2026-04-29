namespace Zlink.Framework.Backend.Contracts;

internal interface IZLinkSpotBackendAdapter
{
    IZLinkBackendSpotNode CreateSpotNode(IZLinkBackendContext context);
}
