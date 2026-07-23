namespace Zlink.Framework.Contracts.Configuration;

public enum ZLinkFrameworkRuntimeState
{
    Preparing = 0,
    Serving = 1,
    Retiring = 2,
    Draining = 3,
    Stopped = 4,
    Error = 5
}

public enum ZLinkFrameworkTerminationIntent
{
    Retire = 0,
    Shutdown = 1
}

public enum ZLinkFrameworkTerminationOutcome
{
    Stopped = 0,
    Blocked = 1,
    ForceStopped = 2
}

public enum ZLinkFrameworkTerminationReason
{
    None = 0,
    TargetUnavailable = 1,
    StoreUnavailable = 2,
    RelocationDisabled = 3,
    StateIncompatible = 4,
    DeadlineExceeded = 5,
    RelocationFailed = 6,
    TeardownFailed = 7,
    RuntimeNotReady = 8
}

public readonly record struct ZLinkFrameworkTerminationResult(
    ZLinkFrameworkTerminationIntent EffectiveIntent,
    ZLinkFrameworkTerminationOutcome Outcome,
    ZLinkFrameworkTerminationReason Reason);

public sealed record ZLinkFrameworkRuntimeSnapshot(
    ZLinkFrameworkRuntimeState State,
    ZLinkFrameworkTerminationIntent? EffectiveIntent,
    DateTimeOffset? Deadline,
    bool WorkSealed,
    ZLinkFrameworkTerminationReason? BlockerReason,
    ulong PendingRequestCount,
    ulong PendingRelocationCount,
    ulong PendingStreamBarrierCount,
    ZLinkFrameworkTerminationResult? TerminalResult,
    ulong Sequence,
    DateTimeOffset ObservedAt);

public sealed record ZLinkFrameworkRuntimeEvent(
    string Identifier,
    ulong Sequence,
    DateTimeOffset Timestamp,
    ZLinkFrameworkRuntimeState State,
    ZLinkFrameworkTerminationIntent? EffectiveIntent,
    ZLinkFrameworkTerminationOutcome? Outcome,
    ZLinkFrameworkTerminationReason? Reason);

public interface IZLinkFrameworkRuntime
{
    ZLinkFrameworkRuntimeState State { get; }

    bool IsReady { get; }

    ZLinkFrameworkRuntimeSnapshot Snapshot();

    IAsyncEnumerable<ZLinkFrameworkRuntimeEvent> ObserveAsync(
        int capacity = 1024,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkFrameworkTerminationResult> RetireAsync(
        TimeSpan? deadline = null,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkFrameworkTerminationResult> ShutdownAsync(
        TimeSpan? deadline = null,
        CancellationToken cancellationToken = default);
}

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

[Obsolete(
    "Use IZLinkFrameworkRuntime.ShutdownAsync instead.",
    DiagnosticId = "ZLINK1100")]
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
