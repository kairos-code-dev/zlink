using System;
using Xunit;

namespace Zlink.Tests;

public sealed class test_message
{
    [Fact]
    public void message_move_transfers_ownership_and_payload()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var source = Message.FromBytes("move-payload"u8);
        using Message moved = source.Move();

        Assert.True(moved.AsReadOnlySpan().SequenceEqual("move-payload"u8));
        Assert.Throws<ObjectDisposedException>(() =>
        {
            _ = source.Size;
        });
    }

    [Fact]
    public void message_move_on_disposed_source_throws()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var source = Message.FromBytes("x"u8);
        source.Dispose();

        Assert.Throws<ObjectDisposedException>(() =>
        {
            _ = source.Move();
        });
    }

    [Fact]
    public void message_move_cannot_be_called_twice()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var source = Message.FromBytes("double-move"u8);
        using Message moved = source.Move();

        Assert.True(moved.AsReadOnlySpan().SequenceEqual("double-move"u8));
        Assert.Throws<ObjectDisposedException>(() =>
        {
            _ = source.Move();
        });
    }
}
