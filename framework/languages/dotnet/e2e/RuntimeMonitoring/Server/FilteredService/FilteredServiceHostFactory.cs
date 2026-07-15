using RuntimeMonitoring.Server.Service.Support;
using Zlink.Framework.Contracts.Eventing;

namespace RuntimeMonitoring.Server.FilteredService;

internal static class FilteredServiceHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var host = ChannelMonitoringRoleHost.Create(args, "filtered-service");
        host.ConfigureMonitoring(ZLinkSocketEventKind.ConnectionReady);
        return host.Build();
    }
}
