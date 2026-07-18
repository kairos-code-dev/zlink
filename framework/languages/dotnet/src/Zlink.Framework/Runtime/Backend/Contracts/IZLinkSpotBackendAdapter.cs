namespace Zlink.Framework.Runtime.Backend.Contracts;

internal interface IZLinkSpotBackendAdapter
{
    IZLinkBackendSpotNode CreateSpotNode(IZLinkBackendContext context);
}