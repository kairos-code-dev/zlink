using Microsoft.AspNetCore.Mvc;
using ResilienceLifecycle.Shared;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;

namespace ResilienceLifecycle.Server.Provider;

internal static class ProviderEndpoints
{
    public static WebApplication MapProviderEndpoints(this WebApplication app, ServerOptions options)
    {
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
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
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
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
        app.MapPost("/admin/weight/exclude", (
            [FromServices] IZLinkChannelRuntimeOptions runtimeOptions,
            [FromServices] EvidenceStore evidence) =>
        {
            runtimeOptions.ClientServerChannel(ResilienceLifecycleNames.Channel).ConfigureServerSocket().Weight = 0;
            evidence.Add($"admin|rid={evidence.Rid}|action=weight-exclude|weight=0");
            return Results.Ok(new { status = "excluded", weight = 0 });
        });
        app.MapPost("/admin/weight/include", (
            [FromServices] IZLinkChannelRuntimeOptions runtimeOptions,
            [FromServices] EvidenceStore evidence) =>
        {
            runtimeOptions.ClientServerChannel(ResilienceLifecycleNames.Channel).ConfigureServerSocket().Weight = 100;
            evidence.Add($"admin|rid={evidence.Rid}|action=weight-include|weight=100");
            return Results.Ok(new { status = "included", weight = 100 });
        });
        app.MapPost("/admin/graceful-drain", async (
            [FromServices] IZLinkDrainControl drain,
            CancellationToken cancellationToken) =>
        {
            var result = await drain.DrainAsync(TimeSpan.FromSeconds(30), cancellationToken);
            return Results.Ok(result switch
            {
                Drained => new DrainResultRes(nameof(Drained)),
                ForceStopped forced => new DrainResultRes(nameof(ForceStopped), forced.Reason.ToString()),
                _ => throw new InvalidOperationException($"Unknown drain result '{result.GetType().Name}'.")
            });
        });
        app.MapGet("/admin/weight", ([FromServices] IZLinkChannelRuntimeOptions runtimeOptions) =>
        {
            var weight = runtimeOptions.ClientServerChannel(ResilienceLifecycleNames.Channel).ConfigureServerSocket()
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
                var weight = runtimeOptions.ClientServerChannel(ResilienceLifecycleNames.Channel)
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
