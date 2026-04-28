namespace Zlink.Framework.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendContextWrapper(global::Zlink.Context nativeContext) : IZLinkBackendContext
{
    public object NativeInstance => nativeContext;

    public ValueTask DisposeAsync() => nativeContext.DisposeAsync();
}
