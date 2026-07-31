using Microsoft.Extensions.Hosting;
using Zlink.Framework.Contracts.Configuration;

namespace ObservabilityOps.Server.Play.Support;

/// <summary>
/// Records every host status the runtime publishes. Spec 24 §3 exposes state
/// changes as an observation stream precisely because intermediate states -
/// relocating among them - can pass faster than any snapshot poll, so a
/// scenario that needs to see one reads this evidence instead of racing it.
/// </summary>
internal sealed class HostStateEvidenceObserver(
    IZLinkFrameworkRuntime runtime,
    EvidenceStore evidence,
    string rid) : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        try
        {
            await foreach (var status in runtime.ObserveAsync(stoppingToken))
                evidence.Add($"host-state|rid={rid}|state={status.State}");
        }
        catch (OperationCanceledException)
        {
            // Host shutdown ends the observation; it owns no runtime decision.
        }
    }
}
