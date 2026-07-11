using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using RuntimeMonitoring.Shared;
using Zlink.Framework.Contracts.Channels;
using RuntimeMonitoring.Server.Trigger.Support;

namespace RuntimeMonitoring.Server.Trigger;

internal static class TriggerEndpoints
{
    public static void Map(WebApplication app, TriggerOptions options)
    {
        app.MapGet("/health", () => Results.Ok(new { status = "ready" }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        app.MapPost("/evidence/wait", async (
            EvidenceWaitReq request,
            EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            var snapshot = await evidence.WaitUntilAsync(
                entries => request.ContainsAll.All(expected =>
                        entries.Any(entry => entry.Contains(expected, StringComparison.Ordinal)))
                    && request.ContainsAnyGroups.All(group =>
                        group.Any(expected =>
                            entries.Any(entry => entry.Contains(expected, StringComparison.Ordinal)))),
                timeout,
                cancellationToken);
            return Results.Ok(snapshot);
        });
        app.MapPost("/profile/request", async (
            ProfileReq request,
            IZLinkChannelClient channel) =>
        {
            var reply = await channel.RequestToChannel(RuntimeMonitoringNames.Channel, request)
                .Timeout(TimeSpan.FromSeconds(3))
                .Async<ProfileRes>();
            return Results.Ok(reply);
        });
        app.MapPost("/profile/request/disconnect", async (ProfileReq request) =>
        {
            var reply = await TriggerClientRequests.RequestWithTransientHostAsync(
                options,
                options.ServiceChannelEndpoint,
                $"trigger-{request.Marker}",
                request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/request/service-b", async (ProfileReq request) =>
        {
            var reply = await TriggerClientRequests.RequestWithTransientHostAsync(
                options,
                options.ServiceBChannelEndpoint,
                $"trigger-service-b-{request.Marker}",
                request);
            return Results.Ok(reply);
        });
        app.MapPost("/profile/request/throw", async (ProfileReq request) =>
        {
            var reply = await TriggerClientRequests.RequestWithTransientHostAsync(
                options,
                options.ThrowChannelEndpoint,
                $"trigger-throw-{request.Marker}",
                request);
            return Results.Ok(reply);
        });
        app.MapGet("/logs/throw-stderr", () =>
        {
            var path = Path.Combine(options.LogDir, "svc-throw.stderr.log");
            return Results.Ok(File.Exists(path) ? File.ReadAllLines(path) : []);
        });
        app.MapPost("/logs/throw-stderr/wait", async (
            EvidenceWaitReq request,
            CancellationToken cancellationToken) =>
        {
            var path = Path.Combine(options.LogDir, "svc-throw.stderr.log");
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            var lines = await TriggerLogReader.WaitForLinesAsync(
                path,
                entries => request.ContainsAll.All(expected =>
                        entries.Any(entry => entry.Contains(expected, StringComparison.Ordinal)))
                    && request.ContainsAnyGroups.All(group =>
                        group.Any(expected =>
                            entries.Any(entry => entry.Contains(expected, StringComparison.Ordinal)))),
                timeout,
                cancellationToken);
            return Results.Ok(lines);
        });
        app.MapPost("/socket/handshake-failure", async () =>
        {
            await TriggerClientRequests.SendInvalidHandshakeAsync(options.ServiceChannelEndpoint);
            return Results.Ok(new { triggered = true });
        });
        app.MapPost("/validation/registration/duplicate-source", () =>
            Results.Ok(TriggerValidation.VerifyDuplicateSocketSource()));
        app.MapPost("/validation/registration/interval", () =>
            Results.Ok(TriggerValidation.VerifyPollingInterval()));
        app.MapPost("/validation/registration/missing-spot", async () =>
            Results.Ok(await TriggerValidation.VerifyMissingSpotSourceAsync()));
        app.MapPost("/validation/registration/missing-socket", async () =>
            Results.Ok(await TriggerValidation.VerifyMissingSocketSourceAsync()));
    }
}
