using Microsoft.AspNetCore.Builder;
using RuntimeMonitoring.Server.Service;

namespace RuntimeMonitoring.Server.FilteredService;

internal static class FilteredServiceHostFactory
{
    public static WebApplication Create(string[] args)
        => ServiceHostFactory.CreateSocketFilter(args);
}
