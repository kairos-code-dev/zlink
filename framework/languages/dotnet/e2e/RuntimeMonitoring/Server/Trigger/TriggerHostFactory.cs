using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using RuntimeMonitoring.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Eventing;
using Zlink.Framework.Contracts.Handlers;

namespace RuntimeMonitoring.Trigger;

internal static class TriggerHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = TriggerOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(new EvidenceStore());
        builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSocketEvent>, SocketEventRecorder>();
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, "trigger-flow.log"))
                .TraceLabel("trigger");
            framework.AddClientServerChannel(RuntimeMonitoringNames.Channel)
                .EnableClient(options.ServiceChannelEndpoint);
        });
        builder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddSocketEvents(RuntimeMonitoringNames.ChannelClientSource);
        });

        var app = builder.Build();
        TriggerEndpoints.Map(app, options);
        return app;
    }
}
