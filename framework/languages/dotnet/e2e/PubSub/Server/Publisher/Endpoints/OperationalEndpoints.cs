namespace PubSub.Server.Publisher.Endpoints;

using PubSub.Shared;
using Zlink.Framework.Contracts.Configuration;

public static class OperationalEndpoints
{
    public static WebApplication MapOperationalEndpoints(this WebApplication app, string role, string rid)
    {
        app.MapGet("/health", () => Results.Ok(new { status = "ready", role, rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        app.MapPost("/evidence/clear", (EvidenceStore evidence) =>
        {
            evidence.Clear();
            return Results.Ok(new { status = "cleared" });
        });
        app.MapPost("/admin/drain", async (
            IZLinkDrainControl drain,
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
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
        return app;
    }
}
