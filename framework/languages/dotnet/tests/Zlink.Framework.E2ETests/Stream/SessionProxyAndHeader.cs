using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text;
using System.Text.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests;

public sealed class SessionProxyAndHeaderTests : StreamTestSupport
{
    [Fact]
    public void SessionProxy_Uses_Multipart_Routed_Client_Push()
    {
        var routeHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            "gateway",
            ZLinkInternalPacketNames.SessionProxy,
            ZLinkEnvelopeCodec.DefaultContentType,
            "correlation",
            DateTimeOffset.UtcNow.AddSeconds(5),
            null,
            null,
            null);
        var parts = new[]
        {
            ZLinkEnvelopeCodec.EncodeHeader(routeHeader),
            ZLinkEnvelopeCodec.EncodePart(new ZLinkSessionProxyEnvelope(
                "player-1",
                "binding-token",
                "client.echo",
                true,
                new Dictionary<string, string>(StringComparer.Ordinal)
                {
                    ["trace-id"] = "trace-1"
                })),
            Message.FromString("{\"value\":\"ping\"}")
        };

        try
        {
            Assert.Equal(routeHeader, ZLinkEnvelopeCodec.DecodeHeader(parts));
            var envelope = ZLinkEnvelopeCodec.DecodePart<ZLinkSessionProxyEnvelope>(parts[1]);
            Assert.Equal("player-1", envelope.ActorId);
            Assert.Equal("client.echo", envelope.PacketName);
            Assert.True(envelope.ExpectsReply);
            Assert.Equal("{\"value\":\"ping\"}", Encoding.UTF8.GetString(parts[2].AsReadOnlySpan()));
            Assert.Null(typeof(ZLinkEnvelopeCodec).GetMethod(
                "Encode",
                BindingFlags.Public | BindingFlags.Static));
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }
    }

    [Fact]
    public async Task HeaderStreamSession_Receives_Replies_And_Tracks_Lifecycle()
    {
        var endpoint = GetFreeTcpEndpoint();
        var recorder = new HeaderStreamRecorder();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var host = await CreateHostAsync(endpoint, services =>
        {
            services.AddSingleton(recorder);
            services.AddZLinkFramework(options =>
            {
                options.AddStreamNode("header.node", stream =>
                {
                    stream.Bind(endpoint);
                    stream.AddHeaderSession<HeaderStreamSession>();
                });
            });
        });
        try
        {
            using var client = ConnectRawClient(endpoint);
            var clientLocalPort = ((IPEndPoint)client.Client.LocalEndPoint!).Port;
            var network = client.GetStream();
            try
            {
                await RetryAsync(
                    async () =>
                    {
                        SendAll(network, BuildStreamPacketFrame(
                            new ZlinkStreamHeader(
                                ZlinkStreamMessageKind.Request,
                                ZlinkStreamCodec.Json,
                                ZlinkStreamHeaderFlags.HasRequestSeq,
                                new ZlinkStreamRequestSeq(1),
                                "ping",
                                ZlinkStreamMetadata.Empty),
                            "\"ping\""u8));
                        await Task.Yield();
                        callbackCapture.ThrowIfAny();
                        return recorder.ReceivedPayloads.Contains("ping");
                    },
                    received => received,
                    TimeSpan.FromSeconds(5));
            }
            catch (Exception ex) when (ex is TimeoutException or AggregateException)
            {
                throw new TimeoutException(
                    $"STREAM raw retry timed out. Connected={recorder.ConnectedCount}, Disconnected={recorder.DisconnectedCount}, Errors={recorder.ErrorCount}, Received={string.Join(',', recorder.ReceivedPayloads)}",
                    ex);
            }

            await RetryAsync(
                () => recorder.LastSessionId is not null
                    && recorder.LastRoutingId is not null
                    && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            callbackCapture.ThrowIfAny();
            var reply = ReceiveFrame(network, new ZlinkStreamRequestSeq(1));
            Assert.True(
                reply.Header.Kind == ZlinkStreamMessageKind.Response,
                Encoding.UTF8.GetString(reply.Payload));
            Assert.Equal(new ZlinkStreamRequestSeq(1), reply.Header.RequestSeq);
            Assert.Equal("\"pong\"", Encoding.UTF8.GetString(reply.Payload));

            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Request,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.HasRequestSeq,
                    new ZlinkStreamRequestSeq(2),
                    "ping",
                    ZlinkStreamMetadata.Empty),
                "\"ping-2\""u8));
            await RetryAsync(
                () => recorder.ReceivedPayloads.Contains("ping-2") && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            callbackCapture.ThrowIfAny();
            var secondReply = ReceiveFrame(network, new ZlinkStreamRequestSeq(2));
            Assert.Equal(ZlinkStreamMessageKind.Response, secondReply.Header.Kind);
            Assert.Equal(new ZlinkStreamRequestSeq(2), secondReply.Header.RequestSeq);
            Assert.Equal("\"pong\"", Encoding.UTF8.GetString(secondReply.Payload));

            Assert.NotNull(recorder.LastSessionId);
            Assert.Equal(recorder.LastSessionId, recorder.ConstructorContextSessionId);
            Assert.NotNull(recorder.LastRoutingId);
            AssertStreamMetadata(endpoint, clientLocalPort, recorder.LastLocalAddr, recorder.LastRemoteAddr);
            Assert.True(
                recorder.ConnectedCount == 1,
                $"Expected one connected callback, got {recorder.ConnectedCount}. Connected sessions={string.Join(',', recorder.ConnectedSessionIds)}.");

            client.Dispose();
            await RetryAsync(
                () => recorder.DisconnectedCount > 0 && recorder.ErrorCount > 0,
                TimeSpan.FromSeconds(5));
            Assert.Equal(ZLinkStreamSessionError.TransportError, recorder.LastError?.Error);
            Assert.NotNull(recorder.LastSessionId);
            Assert.Equal(1, recorder.MaxConcurrentCallbacksFor(recorder.LastSessionId));
        }
        finally
        {
            await host.StopAsync();
            host.Dispose();
        }
    }

    [Fact]
    public async Task HeaderStreamSession_Responds_To_Heartbeat_Control_Ping()
    {
        var endpoint = GetFreeTcpEndpoint();
        var recorder = new HeaderStreamRecorder();

        var host = await CreateHostAsync(endpoint, services =>
        {
            services.AddSingleton(recorder);
            services.AddZLinkFramework(options =>
            {
                options.AddStreamNode("header.node", stream =>
                {
                    stream.Bind(endpoint);
                    stream.AddHeaderSession<HeaderStreamSession>();
                });
            });
        });
        try
        {
            using var client = ConnectRawClient(endpoint);
            var network = client.GetStream();

            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Control,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "$zlink.heartbeat.ping",
                    ZlinkStreamMetadata.Empty),
                ReadOnlySpan<byte>.Empty));

            var reply = ReceiveFrame(network);
            Assert.Equal(ZlinkStreamMessageKind.Control, reply.Header.Kind);
            Assert.Equal("$zlink.heartbeat.pong", reply.Header.Name);
            Assert.Empty(reply.Payload);
            Assert.Empty(recorder.ReceivedPayloads);
        }
        finally
        {
            await host.StopAsync();
            host.Dispose();
        }
    }

    [Fact]
    public async Task HeaderStreamSession_Can_Close_Current_Client_Stream()
    {
        var endpoint = GetFreeTcpEndpoint();
        var recorder = new HeaderStreamRecorder();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var host = await CreateHostAsync(endpoint, services =>
        {
            services.AddSingleton(recorder);
            services.AddZLinkFramework(options =>
            {
                options.AddStreamNode("header.node", stream =>
                {
                    stream.Bind(endpoint);
                    stream.AddHeaderSession<HeaderStreamSession>();
                });
            });
        });
        try
        {
            using var client = ConnectRawClient(endpoint);
            var network = client.GetStream();

            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Request,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.HasRequestSeq,
                    new ZlinkStreamRequestSeq(1),
                    "close",
                    ZlinkStreamMetadata.Empty),
                "\"close\""u8));

            await RetryAsync(
                () => recorder.ReceivedPayloads.Contains("close")
                    && recorder.DisconnectedCount > 0
                    && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            callbackCapture.ThrowIfAny();
            Assert.Equal(0, recorder.ErrorCount);
        }
        finally
        {
            await host.StopAsync();
            host.Dispose();
        }
    }

}
