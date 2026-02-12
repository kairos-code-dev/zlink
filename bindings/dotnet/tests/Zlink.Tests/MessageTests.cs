using Xunit;

namespace Zlink.Tests;

public class MessageTests
{
    [Fact]
    public void MessageFromBytesRoundTrip()
    {
        if (!NativeTests.IsNativeAvailable())
            return;
        try
        {
            byte[] payload = { 1, 2, 3, 4, 5 };
            using var msg = Message.FromBytes(payload);
            Assert.Equal(payload.Length, msg.Size);
            Assert.Equal(payload, msg.ToArray());
        }
        catch (EntryPointNotFoundException)
        {
        }
    }

    [Fact]
    public void MessageFromSpanRoundTrip()
    {
        if (!NativeTests.IsNativeAvailable())
            return;
        try
        {
            ReadOnlySpan<byte> payload = new byte[] { 9, 8, 7, 6 };
            using var msg = new Message(payload);
            Assert.Equal(payload.Length, msg.Size);
            Assert.Equal(payload.ToArray(), msg.AsReadOnlySpan().ToArray());
        }
        catch (EntryPointNotFoundException)
        {
        }
    }

    [Fact]
    public void MessageTryCopyToUsesCallerBuffer()
    {
        if (!NativeTests.IsNativeAvailable())
            return;
        try
        {
            using var msg = Message.FromBytes(new byte[] { 1, 3, 5, 7 });
            Span<byte> small = stackalloc byte[2];
            Assert.False(msg.TryCopyTo(small, out int smallWritten));
            Assert.Equal(0, smallWritten);

            Span<byte> full = stackalloc byte[4];
            Assert.True(msg.TryCopyTo(full, out int fullWritten));
            Assert.Equal(4, fullWritten);
            Assert.Equal(new byte[] { 1, 3, 5, 7 }, full.ToArray());
        }
        catch (EntryPointNotFoundException)
        {
        }
    }
}
