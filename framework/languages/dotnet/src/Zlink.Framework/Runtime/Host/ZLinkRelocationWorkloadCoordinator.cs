using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.Runtime.Host;

internal readonly record struct ZLinkRelocationWorkloadDrainControl(
    Func<bool> StopRequested,
    CancellationToken CancellationToken);

internal sealed class ZLinkRelocationWorkloadCoordinator(
    Func<ZLinkSpotRelocationPhase, CancellationToken,
        ValueTask<ZLinkSpotDrainResult>> drainSpots,
    Func<CancellationToken, ValueTask<ZLinkActorDrainResult>> drainActors)
{
    internal async ValueTask<ZLinkRelocationWorkloadDrainResult> DrainAsync(
        ZLinkRelocationWorkloadDrainControl control)
    {
        var shells = await drainSpots(
                ZLinkSpotRelocationPhase.PerActorShells,
                control.CancellationToken)
            .ConfigureAwait(false);
        ZLinkFrameworkDebugLog.SpotDiscovery(
            $"relocation_phase_per_actor completed={shells.Completed} committed={shells.CommittedUnitCount}");
        if (!shells.Completed || control.StopRequested())
            return new ZLinkRelocationWorkloadDrainResult(
                false,
                shells.TerminalReason,
                shells.CommittedUnitCount);

        var actors = await drainActors(control.CancellationToken)
            .ConfigureAwait(false);
        ZLinkFrameworkDebugLog.SpotDiscovery(
            $"relocation_phase_actors completed={actors.Completed} committed={actors.CommittedUnitCount} reason={actors.TerminalReason}");
        var committed = checked(
            shells.CommittedUnitCount + actors.CommittedUnitCount);
        if (!actors.Completed || actors.TerminalReason is not null)
            return new ZLinkRelocationWorkloadDrainResult(
                actors.Completed,
                actors.TerminalReason,
                committed);
        if (control.StopRequested())
            return new ZLinkRelocationWorkloadDrainResult(
                false,
                null,
                committed);

        var aggregates = await drainSpots(
                ZLinkSpotRelocationPhase.Aggregates,
                control.CancellationToken)
            .ConfigureAwait(false);
        ZLinkFrameworkDebugLog.SpotDiscovery(
            $"relocation_phase_aggregates completed={aggregates.Completed} committed={aggregates.CommittedUnitCount}");
        return new ZLinkRelocationWorkloadDrainResult(
            aggregates.Completed,
            null,
            checked(committed + aggregates.CommittedUnitCount));
    }
}
