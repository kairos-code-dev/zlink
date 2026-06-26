using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using RegistrationCodec.Server.Configuration;
using RegistrationCodec.Server.Infrastructure;

namespace RegistrationCodec.Server.Endpoints;

internal static class OperationalEndpoints
{
    public static WebApplication MapOperationalEndpoints(this WebApplication app, ServerOptions options)
    {
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        app.MapPost("/evidence/clear", (EvidenceStore evidence) =>
        {
            evidence.Clear();
            return Results.Ok(new { status = "cleared" });
        });
        return app;
    }
}
