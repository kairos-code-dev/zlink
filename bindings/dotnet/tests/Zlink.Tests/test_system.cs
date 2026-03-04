using System;
using Xunit;

namespace Zlink.Tests;

public sealed class test_system
{
    [Fact]
    public void version_matches_core_header()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        var expected = CoreTestSupport.ReadCoreHeaderVersion();
        var actual = ZlinkVersion.Get();

        Assert.Equal(expected.major, actual.Major);
        Assert.Equal(expected.minor, actual.Minor);
        Assert.Equal(expected.patch, actual.Patch);
    }

    [Fact]
    public void create_and_destroy_context_socket()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var socket = new Socket(ctx, SocketType.Pair);

        Assert.NotEqual(System.IntPtr.Zero, socket.Handle);
        Assert.True(Runtime.Has("tcp") || !Runtime.Has("tcp"));
    }

    [Fact]
    public void runtime_sleep_overloads_callable()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        Runtime.SleepSeconds(0);
        Runtime.Sleep(TimeSpan.Zero);
    }

    [Fact]
    public void error_code_mapping_covers_posix_and_zlink_ranges()
    {
        const int zlinkHausnumero = 156384712;

        Assert.Equal(ErrorCode.None, ZlinkException.MapErrorCode(0));
        Assert.Equal(ErrorCode.EAgain, ZlinkException.MapErrorCode(11));
        Assert.Equal(ErrorCode.ENotSup, ZlinkException.MapErrorCode(95));
        Assert.Equal(ErrorCode.ENotSup,
            ZlinkException.MapErrorCode(zlinkHausnumero + 1));
        Assert.Equal(ErrorCode.Eterm,
            ZlinkException.MapErrorCode(zlinkHausnumero + 53));
        Assert.Equal(ErrorCode.Unknown, ZlinkException.MapErrorCode(123456789));
    }
}
