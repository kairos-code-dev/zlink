namespace Zlink.Framework.Backend.Contracts;

internal interface IZLinkStreamBackendAdapter
{
    IZLinkBackendStreamSocket CreateStreamSocket(IZLinkBackendContext context);
}
