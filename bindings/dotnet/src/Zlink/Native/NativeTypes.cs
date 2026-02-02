using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Zlink.Native;

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkMsg
{
    public fixed byte Data[64];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkRoutingId
{
    public byte Size;
    public fixed byte Data[255];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkMonitorEvent
{
    public ulong Event;
    public ulong Value;
    public ZlinkRoutingId RoutingId;
    public fixed byte LocalAddr[256];
    public fixed byte RemoteAddr[256];
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ZlinkProviderInfo
{
    public fixed byte ServiceName[256];
    public fixed byte Endpoint[256];
    public ZlinkRoutingId RoutingId;
    public uint Weight;
    public ulong RegisteredAt;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkPollItemUnix
{
    public IntPtr Socket;
    public int Fd;
    public short Events;
    public short Revents;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ZlinkPollItemWindows
{
    public IntPtr Socket;
    public ulong Fd;
    public short Events;
    public short Revents;
}

internal static class NativeHelpers
{
    public static unsafe string ReadString(byte* buffer, int maxLen)
    {
        if (buffer == null || maxLen <= 0)
            return string.Empty;
        int len = 0;
        while (len < maxLen && buffer[len] != 0)
            len++;
        if (len == 0)
            return string.Empty;
        return Encoding.UTF8.GetString(new ReadOnlySpan<byte>(buffer, len));
    }

    public static unsafe byte[] ReadRoutingId(ref ZlinkRoutingId id)
    {
        int size = id.Size;
        if (size <= 0)
            return Array.Empty<byte>();
        byte[] data = new byte[size];
        fixed (byte* src = id.Data)
        {
            new ReadOnlySpan<byte>(src, size).CopyTo(data);
        }
        return data;
    }

    public static unsafe string ReadFixedString(ref ZlinkProviderInfo info, bool service)
    {
        if (service)
        {
            fixed (byte* ptr = info.ServiceName)
            {
                return ReadString(ptr, 256);
            }
        }
        fixed (byte* ptr = info.Endpoint)
        {
            return ReadString(ptr, 256);
        }
    }
}
