namespace Zlink.Framework.Contracts.Configuration;

public enum ZLinkDrainForceReason
{
    DeadlineExceeded = 0,
    DrainingStatePublishFailed = 1,
    OwnerCleanupFailed = 2,
    TeardownFailed = 3
}

public abstract record ZLinkDrainResult
{
    private protected ZLinkDrainResult()
    {
    }
}

public sealed record Drained : ZLinkDrainResult;

public sealed record ForceStopped(ZLinkDrainForceReason Reason) : ZLinkDrainResult;

public interface IZLinkDrainControl
{
    bool IsReady { get; }

    ValueTask<ZLinkDrainResult> DrainAsync(
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkDrainResult> DrainAsync(
        TimeSpan deadline,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkDrainResult> AwaitDrainedAsync(
        CancellationToken cancellationToken = default);
}
