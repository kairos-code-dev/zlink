namespace Zlink.Framework;

public interface IZLinkDispatchOptions
{
    ZLinkDispatchMode SpotDispatchMode { get; set; }

    ZLinkDispatchMode StreamDispatchMode { get; set; }
}
