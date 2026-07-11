using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Hosting;
using LocationMessaging.Server.Workflow.Configuration;
using LocationMessaging.Server.Workflow.Infrastructure;
using LocationMessaging.Shared;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Errors;

namespace LocationMessaging.Server.Workflow.Endpoints;

internal static class WorkflowEndpoints
{
    public static void MapWorkflowEndpoints(this WebApplication app, ServerOptions options)
    {
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        app.MapPost("/workflow/request", async (
            WorkflowReq request,
            IZLinkChannelClient channel) =>
        {
            var reply = await RequestWorkflowWithRetryAsync(channel, request);
            return Results.Ok(reply);
        });
        app.MapPost("/evidence/clear", (EvidenceStore evidence) =>
        {
            evidence.Clear();
            return Results.Ok(new { status = "cleared" });
        });
        app.MapPost("/evidence/wait", async (
            EvidenceWaitReq request,
            EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            var snapshot = await evidence.WaitUntilAsync(
                line => line.Contains(request.Contains, StringComparison.Ordinal),
                timeout,
                cancellationToken);
            return Results.Ok(snapshot);
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
    }

    static async Task<WorkflowRes> RequestWorkflowWithRetryAsync(
        IZLinkChannelClient channel,
        WorkflowReq request)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await channel.RequestToChannel("workflow", request)
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<WorkflowRes>();
            }
            catch (ZLinkFrameworkException ex) when (IsRetriableRequestStartupFailure(ex))
            {
                last = ex;
                await Task.Delay(TimeSpan.FromMilliseconds(100));
            }
        }

        throw new InvalidOperationException("Timed out waiting for workflow request channel route.", last);
    }

    static bool IsRetriableRequestStartupFailure(ZLinkFrameworkException ex) =>
        ex.IsRetriable
        || ex.Kind is ZLinkFrameworkErrorKind.RouteNotConnected
            or ZLinkFrameworkErrorKind.RequestTargetNotFound
            or ZLinkFrameworkErrorKind.RequestProtocolError;
}
