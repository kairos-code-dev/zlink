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
                .Async<EchoReply>(cancellationToken);
            await channel.SendToChannel(RegistrationCodecNames.Channel, new EchoAutoCommand("cmd-rc-a1", "rc-a1-send"))
                .Async(cancellationToken);
            return Results.Ok(reply);
        });
        app.MapPost("/registration/attribute",
            async (IZLinkChannelClient channel, CancellationToken cancellationToken) =>
            {
                var reply = await channel.RequestToChannel(RegistrationCodecNames.Channel, new EchoReq("rc-a2"))
                    .PacketName("EchoAttr")
                    .Async<EchoReply>(cancellationToken);
                await channel.SendToChannel(RegistrationCodecNames.Channel, new EchoCommand("cmd-rc-a2", "rc-a2-send"))
                    .PacketName("EchoAttrCommand")
                    .Async(cancellationToken);
                return Results.Ok(reply);
            });
        app.MapPost("/registration/manual", async (IZLinkChannelClient channel, CancellationToken cancellationToken) =>
        {
            var reply = await channel.RequestToChannel(RegistrationCodecNames.Channel, new EchoManualReq("rc-a3"))
                .Async<EchoReply>(cancellationToken);
            await channel.SendToChannel(RegistrationCodecNames.Channel,
                    new EchoManualCommand("cmd-rc-a3", "rc-a3-send"))
                .Async(cancellationToken);
            return Results.Ok(reply);
        });
        app.MapPost("/registration/di-filter-order",
            async (IZLinkChannelClient channel, CancellationToken cancellationToken) =>
            {
                var first = await channel.RequestToChannel(RegistrationCodecNames.Channel, new EchoReq("rc-a4-1"))
                    .PacketName("EchoDi")
                    .Async<EchoReply>(cancellationToken);
                var second = await channel.RequestToChannel(RegistrationCodecNames.Channel, new EchoReq("rc-a4-2"))
                    .PacketName("EchoDi")
                    .Async<EchoReply>(cancellationToken);
                return Results.Ok(new[] { first, second });
            });
        app.MapPost("/codec/roundtrip", async (IZLinkChannelClient channel, CancellationToken cancellationToken) =>
        {
            var json = await channel.RequestToChannel(RegistrationCodecNames.Channel, new JsonEchoReq("rc-b1"))
                .PacketName("EchoJson")
                .Async<EchoReply>(cancellationToken);
            await channel.SendToChannel(RegistrationCodecNames.Channel, new JsonEchoCommand("cmd-rc-b1", "rc-b1-send"))
                .PacketName("EchoJsonCommand")
                .Async(cancellationToken);

            var protobuf = await channel
                .RequestToChannel(RegistrationCodecNames.Channel, new StringValue { Value = "rc-b2" })
                .PacketName("EchoProtobuf")
                .Async<StringValue>(cancellationToken);
            await channel.SendToChannel(RegistrationCodecNames.Channel, new StringValue { Value = "rc-b2-send" })
                .PacketName("EchoProtobufCommand")
                .Async(cancellationToken);

            var packed = await channel
                .RequestToChannel(RegistrationCodecNames.Channel, new PackedEchoReq { Value = "rc-b3" })
                .PacketName("EchoMessagePack")
                .Async<PackedEchoReq>(cancellationToken);
            await channel.SendToChannel(RegistrationCodecNames.Channel,
                    new PackedEchoCommand { CommandId = "cmd-rc-b3", Value = "rc-b3-send" })
                .PacketName("EchoMessagePackCommand")
                .Async(cancellationToken);

            return Results.Ok(new CodecScenarioResult(json, protobuf.Value, packed.Value));
        });
        return app;
    }
}

internal sealed record CodecScenarioResult(EchoReply Json, string ProtobufValue, string MessagePackValue);