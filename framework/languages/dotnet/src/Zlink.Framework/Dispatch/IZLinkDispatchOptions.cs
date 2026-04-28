namespace Zlink.Framework.Dispatch;

public interface IZLinkDispatchOptions
{
    ZLinkDispatchMode SpotDispatchMode { get; set; }

    ZLinkDispatchMode StreamDispatchMode { get; set; }
}
