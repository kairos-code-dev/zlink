using Google.Protobuf.WellKnownTypes;
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
        return CreateWithMode(args, null);
    }

    private static WebApplication CreateWithMode(
        string[] args,
        Action<WebApplication, ServerOptions>? configureApp)
    {
        var options = ServerOptions.Parse(args) with { CodecMode = "json-only" };
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
            channel.AddRequestHandler<EchoManualRequestHandler, EchoManualReq, EchoRes>("EchoManual");
            channel.AddSendHandler<EchoManualCommandHandler, EchoManualMsg>("EchoManualMsg");
            channel.AddRequestHandler<JsonEchoRequestHandler, JsonEchoReq, EchoRes>("EchoJson");
            channel.AddSendHandler<JsonEchoCommandHandler, JsonEchoMsg>("EchoJsonMsg");
            channel.AddRequestHandler<ProtobufEchoRequestHandler, StringValue, StringValue>();
            channel.AddSendHandler<ProtobufEchoCommandHandler, StringValue>();
            channel.AddRequestHandler<MessagePackEchoRequestHandler, PackedEchoReq, PackedEchoReq>("EchoMessagePack");
            channel.AddSendHandler<MessagePackEchoCommandHandler, PackedEchoMsg>("EchoMessagePackMsg");
            channel.AddRequestHandler<DiEchoRequestHandler, EchoDiReq, EchoRes>("EchoDi");

            if (options.InvalidMode == "duplicate")
                channel.AddRequestHandler<DuplicateEchoRequestHandler, EchoManualReq, EchoRes>("EchoManual");
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
