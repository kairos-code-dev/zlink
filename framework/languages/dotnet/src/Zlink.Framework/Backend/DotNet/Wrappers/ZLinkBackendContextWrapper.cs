namespace Zlink.Framework.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendContextWrapper(Context nativeContext) : IZLinkBackendContext
{
    public object NativeInstance => nativeContext;

    public ValueTask DisposeAsync() => nativeContext.DisposeAsync();
}
