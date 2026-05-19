using System;
using System.Text;
using Xunit;

namespace Systems.Zlink.Tests;

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
    public void message_move_transfers_native_message_payload()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var source = new Message("native-move-payload"u8);
        using Message moved = source.Move();

        Assert.True(moved.AsReadOnlySpan().SequenceEqual(
            "native-move-payload"u8));
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

    [Fact]
    public void native_message_copy_increments_ref_count_for_shared_storage()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        byte[] payload = new byte[512];
        new Random(1234).NextBytes(payload);
        using var source = new Message((ReadOnlySpan<byte>)payload);
        using Message copy = source.Copy();

        Assert.True(source.RefCount >= 2);
        Assert.True(copy.RefCount >= 2);
        Assert.True(copy.AsReadOnlySpan().SequenceEqual(source.AsReadOnlySpan()));
    }

    [Fact]
    public void from_bytes_copy_preserves_managed_snapshot_storage()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        byte[] payload = new byte[512];
        new Random(1234).NextBytes(payload);
        using var source = Message.FromBytes(payload);
        payload[0] ^= 0xFF;

        using Message copy = source.Copy();

        Assert.Equal(1, source.RefCount);
        Assert.Equal(1, copy.RefCount);
        Assert.True(copy.AsReadOnlySpan().SequenceEqual(source.AsReadOnlySpan()));
        Assert.NotEqual(payload[0], source.AsReadOnlySpan()[0]);
    }

    [Fact]
    public void message_string_helpers_round_trip_utf8_payload()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        const string payload = "dotnet-메시지";
        using Message message = Message.FromString(payload, Encoding.UTF8);

        Assert.Equal(payload, message.GetString());
        Assert.True(message.AsReadOnlySpan().SequenceEqual(Encoding.UTF8.GetBytes(payload)));
    }

    [Fact]
    public void message_allocate_exposes_writable_owned_payload()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using Message message = Message.Allocate(3);
        Span<byte> span = message.AsSpan();
        span[0] = 0x01;
        span[1] = 0x02;
        span[2] = 0x03;

        Assert.Equal(new byte[] { 0x01, 0x02, 0x03 }, message.ToArray());
    }

    [Fact]
    public void message_property_accessor_uses_canonical_name()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var message = Message.FromString("property-check");

        Assert.Null(message.GetProperty("Identity"));
    }
}
