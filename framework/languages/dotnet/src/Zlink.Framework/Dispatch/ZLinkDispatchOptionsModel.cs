namespace Zlink.Framework.Dispatch;

internal sealed class ZLinkDispatchOptionsModel : IZLinkDispatchOptions
{
    public ZLinkDispatchMode SpotDispatchMode { get; set; } = ZLinkDispatchMode.Compiled;

    public ZLinkDispatchMode StreamDispatchMode { get; set; } = ZLinkDispatchMode.Compiled;
}
