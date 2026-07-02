namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    /// <summary>Starts the runtime when needed and exposes the started
    /// state so the location auto-connect host can wire its per-mesh loops
    /// to the created channel and spot node runtimes.</summary>
    internal ValueTask<ZLinkFrameworkRuntimeState> EnsureStartedStateAsync(
        CancellationToken cancellationToken) =>
        GetStartedStateAsync(cancellationToken);
}
