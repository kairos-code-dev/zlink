using Microsoft.Extensions.Hosting;
using SupportChat.Server.Configuration;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Samples.Logging;

namespace SupportChat.Server.Api;

public static class ApiServerHostFactory
{
    public static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(
            builder.Logging,
            SampleLogging.DirectoryFromEnvironment("SUPPORTCHAT_LOG_DIR"),
            "api");
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path("api"))
                .TraceLabel("api");
            options.AddHandlersFromAssemblyOf(typeof(ApiServerHostFactory));
            options.AddClientServerChannel(SampleNames.ApiChannel)
                .EnableServer(topology.ApiChannelEndpoint)
                .AddHandlerGroup("api");
            options.AddClientServerChannel(SampleNames.SupportChannel)
                .EnableClient();
        });

        return builder.Build();
    }
}
