namespace Zlink.Framework.UnitTests;

public sealed class RequestFailureMappingTests
{
    [Fact]
    public void Malformed_Envelope_Header_Is_A_Protocol_Error()
    {
        using var header = Message.From("{");

        Assert.Throws<ZLinkEnvelopeProtocolException>(() => ZLinkEnvelopeCodec.DecodeHeader(header));
    }

    [Fact]
    public void Undefined_Envelope_Message_Kind_Is_A_Protocol_Error()
    {
        var invalid = new ZLinkEnvelopeHeader(
            (ZLinkMessageKind)99,
            "route",
            "Lookup",
            ZLinkEnvelopeCodec.DefaultContentType,
            null,
            null,
            null,
            null,
            null)
        {
            FormatMarker = 0xF2
        };
        using var encoded = ZLinkEnvelopeCodec.EncodePart(invalid);

        Assert.Throws<ZLinkEnvelopeProtocolException>(() => ZLinkEnvelopeCodec.DecodeHeader(encoded));
    }

    [Fact]
    public void Spot_Error_Envelope_Preserves_Framework_Error_Kind()
    {
        var parts = ZLinkSpotReplyEnvelope.EncodeErrorParts(
            "spot",
            "Lookup",
            "correlation",
            new ZLinkFrameworkException(ZLinkFrameworkErrorKind.RequestRejected, "draining"));
        try
        {
            var reply = ZLinkEnvelopeCodec.DecodeHeader(parts);
            Assert.Equal(nameof(ZLinkFrameworkErrorKind.RequestRejected), reply.ErrorCode);
            var error = Assert.IsType<ZLinkFrameworkException>(
                ZLinkEnvelopeErrorMapper.CreateException(reply, "fallback"));
            Assert.Equal(ZLinkFrameworkErrorKind.RequestRejected, error.Kind);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }
    }

    [Fact]
    public void ErrorEnvelope_Preserves_Framework_Error_Kind()
    {
        var request = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            "route",
            "Lookup",
            ZLinkEnvelopeCodec.DefaultContentType,
            "correlation",
            null,
            null,
            null,
            null);
        var reply = ZLinkChannelReplyWriter.CreateErrorHeader(
            "route",
            request,
            new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestRejected,
                "draining"));

        Assert.Equal(nameof(ZLinkFrameworkErrorKind.RequestRejected), reply.ErrorCode);
        var error = Assert.IsType<ZLinkFrameworkException>(
            ZLinkEnvelopeErrorMapper.CreateException(reply, "fallback"));
        Assert.Equal(ZLinkFrameworkErrorKind.RequestRejected, error.Kind);
        Assert.Equal("draining", error.Message);
    }

    [Fact]
    public void EnvelopeCompletion_Maps_NotConnected_To_RouteNotConnected()
    {
        Exception? observed = null;
        var reply = Array.Empty<Message>();

        ZLinkEnvelopeReplyCompletion.Complete<string>(
            RequestResult.NotConnected,
            reply,
            _ => throw new InvalidOperationException("Completion should not succeed."),
            error => observed = error,
            "test request");

        var error = Assert.IsType<ZLinkFrameworkException>(observed);
        Assert.Equal(ZLinkFrameworkErrorKind.RouteNotConnected, error.Kind);
        Assert.IsType<ZlinkRequestException>(error.InnerException);
    }

    [Fact]
    public void RawCompletion_Maps_NotFound_To_RequestTargetNotFound()
    {
        Exception? observed = null;
        var reply = Array.Empty<Message>();

        ZLinkRawReplyCompletion.Complete(
            RequestResult.NotFound,
            reply,
            _ => throw new InvalidOperationException("Completion should not succeed."),
            error => observed = error,
            "raw request");

        var error = Assert.IsType<ZLinkFrameworkException>(observed);
        Assert.Equal(ZLinkFrameworkErrorKind.RequestTargetNotFound, error.Kind);
        Assert.IsType<ZlinkRequestException>(error.InnerException);
    }

    [Fact]
    public void RawCompletion_Maps_TimedOut_To_TimeoutException()
    {
        Exception? observed = null;
        var reply = Array.Empty<Message>();

        ZLinkRawReplyCompletion.Complete(
            RequestResult.TimedOut,
            reply,
            _ => throw new InvalidOperationException("Completion should not succeed."),
            error => observed = error,
            "raw request");

        Assert.IsType<TimeoutException>(observed);
    }

    [Fact]
    public async Task SubmitRequestAsync_Fails_NotConnected_Without_Timeout()
    {
        await using var submitter = new ZLinkAsyncSubmitter(
            _ => { },
            TimeSpan.FromSeconds(30),
            CancellationToken.None,
            failFastNotConnected: static () => true);

        var task = submitter.SubmitRequestAsync<string>(
            Message.From("payload"),
            (_, _, _) => throw new ZlinkSubmitException(ZlinkSubmitException.ErrorCode.NotConnected));

        var error = await Assert.ThrowsAsync<ZLinkFrameworkException>(async () => await task.AsTask());
        Assert.Equal(ZLinkFrameworkErrorKind.RouteNotConnected, error.Kind);
        Assert.IsType<ZlinkSubmitException>(error.InnerException);
    }

    [Fact]
    public async Task SubmitRequestAsync_Preserves_Backpressured_On_Submit_Timeout()
    {
        await using var submitter = new ZLinkAsyncSubmitter(
            _ => { },
            TimeSpan.FromMilliseconds(20),
            CancellationToken.None);

        var task = submitter.SubmitRequestAsync<string>(
            Message.From("payload"),
            (_, _, _) => throw new ZlinkSubmitException(ZlinkSubmitException.ErrorCode.Backpressured));

        var error = await Assert.ThrowsAsync<TimeoutException>(async () => await task.AsTask());
        Assert.IsType<ZlinkSubmitException>(error.InnerException);
    }
}
