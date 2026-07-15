using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.Contracts.Locations;

namespace Bingo.Server.Configuration;

public sealed record BingoRoutingIdReport(
    string Role,
    string GroupName,
    IReadOnlyList<string> Members);

public sealed class BingoRoutingIdReporter(
    BingoRoutingIdReport report,
    IZLinkAllocatedRoutingIdProvider allocatedRoutingIds,
    ILogger<BingoRoutingIdReporter> logger) : IHostedService
{
    public async Task StartAsync(CancellationToken cancellationToken)
    {
        var allocation = await allocatedRoutingIds.WaitForReadyAllocationAsync(
            report.GroupName,
            cancellationToken);
        var memberRoutingIds = report.Members
            .Select(member => new KeyValuePair<string, RoutingId>(
                member,
                allocation.MemberRoutingIds[member]))
            .ToArray();
        if (memberRoutingIds.Select(static member => member.Value).Distinct().Count() != 1)
            throw new InvalidOperationException(
                $"Bingo allocation group '{report.GroupName}' did not assign one shared routing id.");

        logger.LogInformation(
            "bingo routing allocation ready. role={Role} group={Group} slot={Slot} members={Members}",
            report.Role,
            report.GroupName,
            allocation.Slot,
            string.Join(',', memberRoutingIds.Select(static member => $"{member.Key}={member.Value}")));
    }

    public Task StopAsync(CancellationToken cancellationToken) => Task.CompletedTask;
}
