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
}
