using Microsoft.Extensions.Configuration;

using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using LocationMessaging.Server.Workflow.Configuration;
using LocationMessaging.Server.Workflow.Endpoints;
using LocationMessaging.Server.Workflow.Handlers;
using LocationMessaging.Server.Workflow.Infrastructure;
using LocationMessaging.Shared;
using Systems.Zlink;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Locations.Redis;

namespace LocationMessaging.Server.Workflow;

internal static class WorkflowHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ServerOptions.Parse(args, defaultRole: "workflow");
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });

        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(new EvidenceStore(options.Rid, options.EvidenceFile));

        builder.Services.AddZLinkFramework(framework =>
        {
            // The official Redis extension registers the peer/spot/actor/route
            // stores and the owner lease store together (doc §2).
            framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint
                    ?? throw new InvalidOperationException("Shared.RedisEndpoint is required."))
                .SetKeyPrefix(options.RedisKeyPrefix
                    ?? throw new InvalidOperationException("Shared.RedisKeyPrefix is required."))));
            framework.ConfigureDispatch()
                .SetMessageFlowObserver<EvidenceDispatchErrorObserver>()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);

            var mesh = framework.AddRouteMesh("workflow")
                .Listen(options.WorkflowEndpoint)
                .SetRoutingIdPrefix(options.Rid);
            var workflow = mesh.Channel("workflow").Server().SetWeight(options.Weight);
            workflow.AddRequestHandler<WorkflowRequestHandler, WorkflowReq, WorkflowRes>("WorkflowReq");
        });

        var app = builder.Build();
        app.MapWorkflowEndpoints(options);
        return app;
    }
}
