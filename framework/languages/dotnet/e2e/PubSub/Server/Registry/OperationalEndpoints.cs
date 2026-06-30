namespace PubSub.Server.Registry;

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
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
        return app;
    }
}