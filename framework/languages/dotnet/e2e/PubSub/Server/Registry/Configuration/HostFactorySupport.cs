using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.Logging;
using PubSub.Server.Registry;

namespace PubSub.Server.Registry.Configuration;

public static class HostFactorySupport
{
    public static WebApplicationBuilder CreateBuilder(string[] args, string httpUrl, string logDir)
    {
        Directory.CreateDirectory(logDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(httpUrl);
        return builder;
    }
}
