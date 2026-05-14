namespace Zlink.Framework.Runtime.Backend.Contracts;

internal interface IZLinkStreamBackendAdapter
{
    IZLinkBackendStreamSocket CreateStreamSocket(IZLinkBackendContext context);
}
