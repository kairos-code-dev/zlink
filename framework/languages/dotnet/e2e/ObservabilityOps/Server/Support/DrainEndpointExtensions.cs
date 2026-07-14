using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Zlink.Framework.Contracts.Configuration;

namespace ObservabilityOps.Server.Support;

public static class DrainEndpointExtensions
{
    public static WebApplication MapDrainOperations(this WebApplication app)
    {
        app.MapPost("/drain", (int? deadlineMs, IZLinkDrainControl drain, DrainOperation operation) =>
            Results.Ok(operation.Start(drain, TimeSpan.FromMilliseconds(deadlineMs ?? 30000))));
        app.MapGet("/drain/status", (DrainOperation operation) => Results.Ok(operation.Snapshot()));
        app.MapPost("/drain/wait", async (int? timeoutMs, DrainOperation operation,
            CancellationToken cancellationToken) => Results.Ok(await operation.WaitAsync(
            TimeSpan.FromMilliseconds(timeoutMs ?? 30000), cancellationToken)));
        return app;
    }
}
