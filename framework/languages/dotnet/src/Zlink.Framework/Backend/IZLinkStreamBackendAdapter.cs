namespace Zlink.Framework.Backend;

internal interface IZLinkStreamBackendAdapter
{
    IZLinkBackendStreamSocket CreateStreamSocket(IZLinkBackendContext context);
}
