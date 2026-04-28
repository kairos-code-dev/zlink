using Zlink.Framework.Backend;

namespace Zlink.Framework.Runtime.Framework;

internal sealed class ZLinkActorRuntimeState
{
    public SemaphoreSlim Gate { get; } = new(1, 1);

    public string? SessionId { get; set; }

    public IZLinkStream? Stream { get; set; }

    public ZLinkSpotActivation? Activation { get; set; }

    public ZLinkActorDispatchState? CurrentDispatch { get; set; }
}
