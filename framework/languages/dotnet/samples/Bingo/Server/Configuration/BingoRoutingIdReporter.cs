using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.Contracts.Configuration;

namespace Bingo.Server.Configuration;

public sealed record BingoRoutingIdReport(
    string Role,
    string MeshName);

public sealed class BingoRoutingIdReporter(
    BingoRoutingIdReport report,
    IZLinkRouteMeshRuntime routeMesh,
    ILogger<BingoRoutingIdReporter> logger) : IHostedService
{
    public Task StartAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var snapshot = routeMesh.Snapshot(report.MeshName);
        logger.LogInformation(
            "bingo mesh identity ready. role={Role} mesh={Mesh} rid={RoutingId}",
            report.Role,
            report.MeshName,
            snapshot.Rid);
        return Task.CompletedTask;
    }

    public Task StopAsync(CancellationToken cancellationToken) => Task.CompletedTask;
}
