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
            channel.SendToChannel(RegistrationCodecNames.Channel, new EchoAutoMsg("cmd-rc-a1", "rc-a1-send")).Submit(cancellationToken);
            return Results.Ok(reply);
        });
        app.MapPost("/registration/attribute",
            async (IZLinkChannelClient channel, CancellationToken cancellationToken) =>
            {
                var reply = await channel.RequestToChannel(RegistrationCodecNames.Channel, new EchoReq("rc-a2"))
                    .PacketName("EchoAttr")
                    .Async<EchoRes>(cancellationToken);
                channel.SendToChannel(RegistrationCodecNames.Channel, new EchoMsg("cmd-rc-a2", "rc-a2-send"))
                    .PacketName("EchoAttrMsg").Submit(cancellationToken);
                return Results.Ok(reply);
            });
        app.MapPost("/registration/manual", async (IZLinkChannelClient channel, CancellationToken cancellationToken) =>
        {
            var reply = await channel.RequestToChannel(RegistrationCodecNames.Channel, new EchoManualReq("rc-a3"))
                .Async<EchoRes>(cancellationToken);
            channel.SendToChannel(RegistrationCodecNames.Channel,
                    new EchoManualMsg("cmd-rc-a3", "rc-a3-send")).Submit(cancellationToken);
            return Results.Ok(reply);
        });
        app.MapPost("/registration/di-filter-order",
            async (IZLinkChannelClient channel, CancellationToken cancellationToken) =>
            {
                var first = await channel.RequestToChannel(RegistrationCodecNames.Channel, new EchoReq("rc-a4-1"))
                    .PacketName("EchoDi")
                    .Async<EchoRes>(cancellationToken);
                var second = await channel.RequestToChannel(RegistrationCodecNames.Channel, new EchoReq("rc-a4-2"))
                    .PacketName("EchoDi")
                    .Async<EchoRes>(cancellationToken);
                return Results.Ok(new[] { first, second });
            });
        app.MapPost("/codec/roundtrip", async (IZLinkChannelClient channel, CancellationToken cancellationToken) =>
        {
            var json = await channel.RequestToChannel(RegistrationCodecNames.Channel, new JsonEchoReq("rc-b1"))
                .PacketName("EchoJson")
                .Async<EchoRes>(cancellationToken);
            channel.SendToChannel(RegistrationCodecNames.Channel, new JsonEchoMsg("cmd-rc-b1", "rc-b1-send"))
                .PacketName("EchoJsonMsg").Submit(cancellationToken);

            var protobuf = await channel
                .RequestToChannel(RegistrationCodecNames.Channel, new StringValue { Value = "rc-b2" })
                .PacketName("EchoProtobuf")
                .Async<StringValue>(cancellationToken);
            channel.SendToChannel(RegistrationCodecNames.Channel, new StringValue { Value = "rc-b2-send" })
                .PacketName("EchoProtobufMsg").Submit(cancellationToken);

            var packed = await channel
                .RequestToChannel(RegistrationCodecNames.Channel, new PackedEchoReq { Value = "rc-b3" })
                .PacketName("EchoMessagePack")
                .Async<PackedEchoReq>(cancellationToken);
            channel.SendToChannel(RegistrationCodecNames.Channel,
                    new PackedEchoMsg { CommandId = "cmd-rc-b3", Value = "rc-b3-send" })
                .PacketName("EchoMessagePackMsg").Submit(cancellationToken);

            return Results.Ok(new CodecScenarioRes(json, protobuf.Value, packed.Value));
        });
        return app;
    }
}

internal sealed record CodecScenarioRes(EchoRes Json, string ProtobufValue, string MessagePackValue);