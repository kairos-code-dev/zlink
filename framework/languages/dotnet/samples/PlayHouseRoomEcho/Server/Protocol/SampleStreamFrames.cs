using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Systems.Zlink.Stream.Connector.Abstractions;
using Systems.Zlink.Stream.Connector.Builders;
using Systems.Zlink.Stream.Connector.Codecs;
using Systems.Zlink.Stream.Connector.Compression;
using Systems.Zlink.Stream.Connector.Connector;
using Systems.Zlink.Stream.Connector.Framing;
using Systems.Zlink.Stream.Connector.Headers;
using Systems.Zlink.Stream.Connector.Metadata;
using Systems.Zlink.Stream.Connector.Options;
using Zlink;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.AspNetCore.Builder;

static class SampleStreamFrames
{
    private static readonly ZlinkStreamHeaderCodec HeaderCodec = new();
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    public static ZlinkStreamHeader DecodeHeader(Message header)
    {
        return HeaderCodec.Decode(header.AsReadOnlyMemory());
    }

    public static Message EncodeHeader(ZlinkStreamHeader header)
    {
        return Message.FromBytes(HeaderCodec.Encode(header).ToArray());
    }

    public static T DecodeBody<T>(Message body)
    {
        return JsonSerializer.Deserialize<T>(body.AsReadOnlyMemory().Span, JsonOptions)
            ?? throw new InvalidOperationException($"Could not decode stream body as {typeof(T).Name}.");
    }

    public static void WriteResponse<TBody>(
        IZLinkStream stream,
        ZlinkStreamHeader requestHeader,
        TBody body)
    {
        if (requestHeader.RequestId is null)
        {
            throw new InvalidOperationException("Cannot write a response for a packet without a request id.");
        }

        var headerBytes = HeaderCodec.Encode(new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Response,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.HasRid,
            requestHeader.RequestId,
            requestHeader.Name,
            ZlinkStreamMetadata.Empty));
        var bodyBytes = JsonSerializer.SerializeToUtf8Bytes(body, JsonOptions);
        var frame = new byte[6 + headerBytes.Length + bodyBytes.Length];
        frame[0] = (byte)(headerBytes.Length >> 8);
        frame[1] = (byte)headerBytes.Length;
        frame[2] = (byte)(bodyBytes.Length >> 24);
        frame[3] = (byte)(bodyBytes.Length >> 16);
        frame[4] = (byte)(bodyBytes.Length >> 8);
        frame[5] = (byte)bodyBytes.Length;
        headerBytes.Span.CopyTo(frame.AsSpan(6));
        bodyBytes.CopyTo(frame.AsSpan(6 + headerBytes.Length));

        using var payload = Message.FromBytes(frame);
        stream.Write(payload);
    }
}
