using Google.Protobuf.WellKnownTypes;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using RegistrationCodec.Server.JsonOnlyPeer;
using RegistrationCodec.Server.JsonOnlyPeer.Handlers;
using RegistrationCodec.Server.JsonOnlyPeer.Infrastructure;
using RegistrationCodec.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Codecs.MessagePack;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Dispatch;

namespace RegistrationCodec.Server.JsonOnlyPeer;

public static class RegistrationCodecServerHostFactory
{
    public static WebApplication Create(string[] args)
    {
        return CreateWithMode(args.Concat(["--codec-mode", "json-only"]).ToArray(), null);
    }

    private static WebApplication CreateWithMode(
        string[] args,
        Action<WebApplication, ServerOptions>? configureApp)
    {
        var options = ServerOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(options);
        builder.Services.AddSingleton(new EvidenceStore(options.EvidenceFile));
        builder.Services.AddSingleton<SingletonProbe>();
        builder.Services.AddScoped<ScopedProbe>();
        builder.Services.AddSingleton<FirstFilter>();
        builder.Services.AddSingleton<SecondFilter>();
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);
            framework.Codecs.AddJson();
            if (options.CodecMode != "json-only")
            {
                framework.Codecs.Use(ZLinkProtobufCodec.Default);
                framework.Codecs.Use(ZLinkMessagePackCodec.Default);
            }

            framework.AddHandlersFromAssemblyOf<EchoAutoRequestHandler>();
            framework.UseFilter<FirstFilter>();
            framework.UseFilter<SecondFilter>();

            var channel = framework.AddClientServerChannel(RegistrationCodecNames.Channel)
                .EnableServer(Require(options.ChannelEndpoint, "--channel-endpoint"))
                .EnableClient(Require(options.ChannelEndpoint, "--channel-endpoint"));
            channel.AddHandlerGroup("auto");
            channel.AddHandlerGroup("attr");
            channel.AddRequestHandler<EchoManualRequestHandler, EchoManualReq, EchoReply>("EchoManual");
            channel.AddSendHandler<EchoManualCommandHandler, EchoManualCommand>("EchoManualCommand");
            channel.AddRequestHandler<JsonEchoRequestHandler, JsonEchoReq, EchoReply>("EchoJson");
            channel.AddSendHandler<JsonEchoCommandHandler, JsonEchoCommand>("EchoJsonCommand");
            channel.AddRequestHandler<ProtobufEchoRequestHandler, StringValue, StringValue>("EchoProtobuf");
            channel.AddSendHandler<ProtobufEchoCommandHandler, StringValue>("EchoProtobufCommand");
            channel.AddRequestHandler<MessagePackEchoRequestHandler, PackedEchoReq, PackedEchoReq>("EchoMessagePack");
            channel.AddSendHandler<MessagePackEchoCommandHandler, PackedEchoCommand>("EchoMessagePackCommand");
            channel.AddRequestHandler<DiEchoRequestHandler, EchoReq, EchoReply>("EchoDi");

            if (options.InvalidMode == "duplicate")
            {
                channel.AddRequestHandler<DuplicateEchoRequestHandler, EchoManualReq, EchoReply>("EchoManual");
            }
        });

        var app = builder.Build();
        app.MapOperationalEndpoints(options);
        configureApp?.Invoke(app, options);
        return app;
    }

    private static string Require(string? value, string name)
    {
        return string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"{name} is required.")
            : value;
    }
}
