using RuntimeMonitoring.Server.Service.Handlers;
using RuntimeMonitoring.Server.Service.Support;

namespace RuntimeMonitoring.Server.ThrowingService;

internal static class ThrowingServiceHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var host = ChannelMonitoringRoleHost.Create(args, "throwing-service");
        host.AddSocketEventHandler<ThrowingSocketEventRecorder>();
        host.ConfigureMonitoring();
        return host.Build();
    }
}
