using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class EnvelopeCodecTests
{
    [Fact]
    public void DecodeBody_Returns_Message_When_BodyType_Is_Message()
    {
        using var body = Message.From("raw-join-request");

        var decoded = ZLinkEnvelopeCodec.DecodeBody(body, typeof(Message));

        Assert.Same(body, decoded);
    }

    [Fact]
    public void EncodeBody_Copies_Message_When_BodyType_Is_Message()
    {
        using var body = Message.From("raw-join-reply");
        using var encoded = ZLinkEnvelopeCodec.EncodeBody(body, typeof(Message));

        Assert.NotSame(body, encoded);
        Assert.Equal(body.ToArray(), encoded.ToArray());
    }
}
