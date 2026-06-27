using RuntimeMonitoring.Shared;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Timers;

namespace RuntimeMonitoring.Server.Service;

internal sealed class ProfileRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<ProfileRequest, ProfileReply>
{
    public ValueTask<ProfileReply> HandleAsync(
        ProfileRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"profile-request|rid={evidence.Rid}|marker={request.Marker}|value={request.Value}");
        return ValueTask.FromResult(new ProfileReply($"profile:{request.Value}", evidence.Rid, request.Marker));
    }
}

internal sealed class MonitoringEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        await Context.AddTimer<FailingTimerHandler>(
            "failing",
            TimeSpan.FromMilliseconds(50),
            new ZLinkTimerOptions { StopOnUnhandledException = false },
            cancellationToken);
        await Context.AddTimer<FailingTimerHandler>(
            "stopping",
            TimeSpan.FromMilliseconds(50),
            new ZLinkTimerOptions { StopOnUnhandledException = true },
            cancellationToken);
    }
}

internal sealed class FailingTimerHandler : IZLinkSpotTimerHandler<MonitoringEntrySpot>
{
    public ValueTask HandleAsync(
        MonitoringEntrySpot spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        throw new InvalidOperationException("monitoring timer failure");
    }
}
