using Google.Protobuf.WellKnownTypes;
using RegistrationCodec.Shared;
using Zlink.Framework.Contracts.Channels;

namespace RegistrationCodec.Server.Main.Endpoints;

internal static class RegistrationScenarioEndpoints
{
    public static WebApplication MapRegistrationScenarioEndpoints(this WebApplication app, ServerOptions options)
    {
        app.MapPost("/registration/auto", async (IZLinkChannelClient channel, CancellationToken cancellationToken) =>
        {
            var reply = await channel.RequestToChannel(RegistrationCodecNames.Channel, new EchoAutoReq("rc-a1"))
                .Async<EchoRes>(cancellationToken);
            channel.SendToChannel(RegistrationCodecNames.Channel, new EchoAutoMsg("cmd-rc-a1", "rc-a1-send")).TrySubmit();
            return Results.Ok(reply);
        });
        app.MapPost("/registration/attribute",
            async (IZLinkChannelClient channel, CancellationToken cancellationToken) =>
            {
                var reply = await channel.RequestToChannel(RegistrationCodecNames.Channel, new EchoAttrReq("rc-a2"))
                    .Async<EchoRes>(cancellationToken);
                channel.SendToChannel(RegistrationCodecNames.Channel, new EchoAttrMsg("cmd-rc-a2", "rc-a2-send")).TrySubmit();
                return Results.Ok(reply);
            });
        app.MapPost("/registration/manual", async (IZLinkChannelClient channel, CancellationToken cancellationToken) =>
        {
            var reply = await channel.RequestToChannel(RegistrationCodecNames.Channel, new EchoManualReq("rc-a3"))
                .Async<EchoRes>(cancellationToken);
            channel.SendToChannel(RegistrationCodecNames.Channel,
                    new EchoManualMsg("cmd-rc-a3", "rc-a3-send")).TrySubmit();
            return Results.Ok(reply);
        });
        app.MapPost("/registration/di-filter-order",
            async (IZLinkChannelClient channel, CancellationToken cancellationToken) =>
            {
                var first = await channel.RequestToChannel(RegistrationCodecNames.Channel, new EchoDiReq("rc-a4-1"))
                    .Async<EchoRes>(cancellationToken);
                var second = await channel.RequestToChannel(RegistrationCodecNames.Channel, new EchoDiReq("rc-a4-2"))
                    .Async<EchoRes>(cancellationToken);
                return Results.Ok(new[] { first, second });
            });
        app.MapPost("/codec/roundtrip", async (IZLinkChannelClient channel, CancellationToken cancellationToken) =>
        {
            var json = await channel.RequestToChannel(RegistrationCodecNames.Channel, new JsonEchoReq("rc-b1"))
                .Async<EchoRes>(cancellationToken);
            channel.SendToChannel(RegistrationCodecNames.Channel, new JsonEchoMsg("cmd-rc-b1", "rc-b1-send")).TrySubmit();

            var protobuf = await channel
                .RequestToChannel(RegistrationCodecNames.Channel, new StringValue { Value = "rc-b2" })
                .Async<StringValue>(cancellationToken);
            channel.SendToChannel(RegistrationCodecNames.Channel, new StringValue { Value = "rc-b2-send" }).TrySubmit();

            var packed = await channel
                .RequestToChannel(RegistrationCodecNames.Channel, new PackedEchoReq { Value = "rc-b3" })
                .Async<PackedEchoReq>(cancellationToken);
            channel.SendToChannel(RegistrationCodecNames.Channel,
                    new PackedEchoMsg { CommandId = "cmd-rc-b3", Value = "rc-b3-send" }).TrySubmit();

            return Results.Ok(new CodecScenarioRes(json, protobuf.Value, packed.Value));
        });
        return app;
    }
}

internal sealed record CodecScenarioRes(EchoRes Json, string ProtobufValue, string MessagePackValue);
