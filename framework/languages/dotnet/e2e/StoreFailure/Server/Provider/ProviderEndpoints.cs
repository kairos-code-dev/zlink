using Microsoft.AspNetCore.Mvc;
using StoreFailure.Shared;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;

namespace StoreFailure.Server.Provider;

internal static class ProviderEndpoints
{
    public static WebApplication MapProviderEndpoints(this WebApplication app, ServerOptions options)
    {
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
        app.MapGet("/query/status", async (
            Zlink.Framework.Contracts.Locations.IZLinkLocationRuntimeQuery query,
            CancellationToken cancellationToken) =>
        {
            var status = await query.GetStatusAsync(cancellationToken);
            return Results.Ok(new RuntimeStatusRes(
                status.StoreHealthy,
                status.WatchEnabled,
                status.LastError,
                status.OwnerLeaseHealthy,
                status.OwnerLeaseRenewedAt,
                status.LastRefreshAt));
        });
        app.MapPost("/query/status/wait", async (
            RuntimeStatusWaitReq request,
            Zlink.Framework.Contracts.Locations.IZLinkLocationRuntimeQuery query,
            CancellationToken cancellationToken) =>
        {
            var response = await RuntimeStatusWaiter.WaitAsync(async token =>
            {
                var status = await query.GetStatusAsync(token);
                return new RuntimeStatusRes(
                    status.StoreHealthy, status.WatchEnabled, status.LastError,
                    status.OwnerLeaseHealthy, status.OwnerLeaseRenewedAt, status.LastRefreshAt);
            }, request, cancellationToken);
            if (response is not null) return Results.Ok(response);

            return Results.Problem("Runtime status did not reach the requested state.",
                statusCode: StatusCodes.Status504GatewayTimeout);
        });
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
        app.MapPost("/evidence/clear", (EvidenceStore evidence) =>
        {
            evidence.Clear();
            return Results.Ok(new { status = "cleared" });
        });
        app.MapPost("/drain", async (IZLinkDrainControl drain) =>
        {
            var result = await drain.DrainAsync(TimeSpan.FromSeconds(30), CancellationToken.None);
            return result switch
            {
                Drained => Results.Ok(new DrainResultRes("drained", null)),
                ForceStopped forced => Results.Ok(
                    new DrainResultRes("force_stopped", forced.Reason.ToString())),
                _ => throw new InvalidOperationException("Unknown drain result.")
            };
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
        app.MapPost("/admin/crash", () =>
        {
            _ = Task.Run(async () =>
            {
                await Task.Delay(50);
                Environment.FailFast("resilience lifecycle e2e crash");
            });
            return Results.Ok(new { status = "crashing" });
        });
        app.MapPost("/admin/fault/{mode}", (
            string mode,
            [FromServices] FaultState fault,
            [FromServices] EvidenceStore evidence) =>
        {
            fault.Mode = mode;
            evidence.Add($"admin|rid={evidence.Rid}|action=fault|mode={mode}");
            return Results.Ok(new { status = "fault", mode });
        });
        app.MapPost("/admin/drain", (
            [FromServices] IZLinkChannelRuntimeOptions runtimeOptions,
            [FromServices] EvidenceStore evidence) =>
        {
            runtimeOptions.ClientServerChannel(StoreFailureNames.Channel).ConfigureServerSocket().Weight = 0;
            evidence.Add($"admin|rid={evidence.Rid}|action=drain|weight=0");
            return Results.Ok(new { status = "drained", weight = 0 });
        });
        app.MapPost("/admin/restore", (
            [FromServices] IZLinkChannelRuntimeOptions runtimeOptions,
            [FromServices] EvidenceStore evidence) =>
        {
            runtimeOptions.ClientServerChannel(StoreFailureNames.Channel).ConfigureServerSocket().Weight = 100;
            evidence.Add($"admin|rid={evidence.Rid}|action=restore|weight=100");
            return Results.Ok(new { status = "restored", weight = 100 });
        });
        app.MapGet("/admin/weight", ([FromServices] IZLinkChannelRuntimeOptions runtimeOptions) =>
        {
            var weight = runtimeOptions.ClientServerChannel(StoreFailureNames.Channel).ConfigureServerSocket()
                .Weight;
            return Results.Ok(new { weight });
        });
        app.MapPost("/admin/weight/wait", async (
            WeightWaitReq request,
            [FromServices] IZLinkChannelRuntimeOptions runtimeOptions,
            CancellationToken cancellationToken) =>
        {
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            var deadline = DateTimeOffset.UtcNow + timeout;
            while (DateTimeOffset.UtcNow < deadline)
            {
                var weight = runtimeOptions.ClientServerChannel(StoreFailureNames.Channel)
                    .ConfigureServerSocket()
                    .Weight;
                if (weight == request.Expected) return Results.Ok(new { weight });

                await Task.Delay(TimeSpan.FromMilliseconds(50), cancellationToken);
            }

            return Results.Problem(
                $"Provider weight did not become {request.Expected}.",
                statusCode: StatusCodes.Status504GatewayTimeout);
        });
        return app;
    }
}
