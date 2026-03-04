// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.InteropServices;
using Zlink.Native;

namespace Zlink;

public sealed class ZlinkException : Exception
{
    public int Errno { get; }

    public ZlinkException(int errno, string message) : base(message)
    {
        Errno = errno;
    }

    public static ZlinkException FromLastError()
    {
        int errno = NativeMethods.zlink_errno();
        IntPtr msgPtr = NativeMethods.zlink_strerror(errno);
        string message = msgPtr == IntPtr.Zero
            ? "zlink error"
            : Marshal.PtrToStringAnsi(msgPtr) ?? "zlink error";
        return new ZlinkException(errno, message);
    }

    public static void ThrowIfError(int rc)
    {
        if (rc < 0)
            throw FromLastError();
    }

    public static bool TryMapErrorCode(int errno, out ErrorCode code)
    {
        switch (errno)
        {
            case (int)ErrorCode.Efsm:
                code = ErrorCode.Efsm;
                return true;
            case (int)ErrorCode.EnoCompatProto:
                code = ErrorCode.EnoCompatProto;
                return true;
            case (int)ErrorCode.Eterm:
                code = ErrorCode.Eterm;
                return true;
            case (int)ErrorCode.EmThread:
                code = ErrorCode.EmThread;
                return true;
            default:
                code = default;
                return false;
        }
    }
}
