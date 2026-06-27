using Microsoft.AspNetCore.Builder;
using RuntimeMonitoring.Server.Service;

namespace RuntimeMonitoring.Server.ThrowingService;

internal static class ThrowingServiceHostFactory
{
    public static WebApplication Create(string[] args)
        => ServiceHostFactory.CreateThrowing(args);
}
