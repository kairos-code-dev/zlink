using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using RegistryMessaging.Server.Workflow.Configuration;
using RegistryMessaging.Server.Workflow.Endpoints;
using RegistryMessaging.Server.Workflow.Handlers;
using RegistryMessaging.Server.Workflow.Infrastructure;
using RegistryMessaging.Shared;
using Systems.Zlink;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;

namespace RegistryMessaging.Server.Workflow;

internal static class WorkflowHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ServerOptions.Parse(args, defaultRole: "workflow");
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });

        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(new EvidenceStore(options.EvidenceFile));

        builder.Services.AddZLinkFramework(framework =>
        {
            framework.UseDiscovery().AddRegistryEndpoint(options.RegistryRouterEndpoint
                ?? throw new InvalidOperationException("--registry-router-endpoint is required."));
            framework.ConfigureDispatch()
                .SetMessageFlowObserver<EvidenceDispatchErrorObserver>()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);

            var workflow = framework.AddClientServerChannel("workflow")
                .EnableServer(options.WorkflowEndpoint)
                .EnableClient()
                .SetRoutingId(RoutingId.From(options.Rid));
            workflow.ConfigureServerSocket().Weight = options.Weight;
            workflow.AddRequestHandler<WorkflowRequestHandler, WorkflowRequest, WorkflowReply>("WorkflowRequest");
        });

        var app = builder.Build();
        app.MapWorkflowEndpoints(options);
        return app;
    }
}
