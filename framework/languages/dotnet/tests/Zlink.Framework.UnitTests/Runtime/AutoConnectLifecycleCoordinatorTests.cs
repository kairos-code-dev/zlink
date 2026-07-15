using System.Runtime.CompilerServices;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Host;

namespace Zlink.Framework.UnitTests;

public sealed class AutoConnectLifecycleCoordinatorTests
{
    [Fact]
    public async Task WithoutSocketMonitoring_FrameworkPhaseOwnsExactlyOneGeneration()
    {
        var starts = 0;
        var stops = 0;
        var coordinator = CreateCoordinator(
            requiresSocketMonitoring: false,
            () => starts++,
            () => stops++);
        var state = UninitializedState();

        await coordinator.FrameworkReadyAsync(state, CancellationToken.None);
        await coordinator.FrameworkReadyAsync(state, CancellationToken.None);
        await coordinator.SocketMonitoringReadyAsync(CancellationToken.None);
        await coordinator.StopAsync(CancellationToken.None);
        await coordinator.StopAsync(CancellationToken.None);

        Assert.Equal(1, starts);
        Assert.Equal(1, stops);
    }

    [Fact]
    public async Task WithSocketMonitoring_MonitorPhasePrecedesExactlyOneGeneration()
    {
        var phases = new List<string>();
        var stops = 0;
        var coordinator = CreateCoordinator(
            requiresSocketMonitoring: true,
            () => phases.Add("auto-connect-started"),
            () => stops++);
        var state = UninitializedState();

        await coordinator.FrameworkReadyAsync(state, CancellationToken.None);
        Assert.Empty(phases);

        phases.Add("socket-monitor-attached");
        await coordinator.SocketMonitoringReadyAsync(CancellationToken.None);
        await coordinator.SocketMonitoringReadyAsync(CancellationToken.None);
        await coordinator.FrameworkReadyAsync(state, CancellationToken.None);
        await coordinator.StopAsync(CancellationToken.None);

        Assert.Equal(
            ["socket-monitor-attached", "auto-connect-started"],
            phases);
        Assert.Equal(1, stops);
    }

    private static ZLinkAutoConnectLifecycleCoordinator CreateCoordinator(
        bool requiresSocketMonitoring,
        Action started,
        Action stopped)
    {
        return new ZLinkAutoConnectLifecycleCoordinator(
            (_, _) =>
            {
                started();
                return ValueTask.CompletedTask;
            },
            _ =>
            {
                stopped();
                return ValueTask.CompletedTask;
            },
            requiresSocketMonitoring);
    }

    private static ZLinkFrameworkRuntimeState UninitializedState() =>
        (ZLinkFrameworkRuntimeState)RuntimeHelpers.GetUninitializedObject(
            typeof(ZLinkFrameworkRuntimeState));
}
