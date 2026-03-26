// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.InteropServices;
using Zlink.Native;

namespace Zlink;

public sealed class ZlinkException : Exception
{
    private const int ZlinkHausnumero = 156384712;
    private const int EnotSupFallback = ZlinkHausnumero + 1;
    private const int EprotoNoSupportFallback = ZlinkHausnumero + 2;
    private const int EnoBufsFallback = ZlinkHausnumero + 3;
    private const int EnetDownFallback = ZlinkHausnumero + 4;
    private const int EaddrInUseFallback = ZlinkHausnumero + 5;
    private const int EaddrNotAvailFallback = ZlinkHausnumero + 6;
    private const int EconnRefusedFallback = ZlinkHausnumero + 7;
    private const int EinProgressFallback = ZlinkHausnumero + 8;
    private const int EnotSockFallback = ZlinkHausnumero + 9;
    private const int EmsgSizeFallback = ZlinkHausnumero + 10;
    private const int EafNoSupportFallback = ZlinkHausnumero + 11;
    private const int EnetUnreachFallback = ZlinkHausnumero + 12;
    private const int EconnAbortedFallback = ZlinkHausnumero + 13;
    private const int EconnResetFallback = ZlinkHausnumero + 14;
    private const int EnotConnFallback = ZlinkHausnumero + 15;
    private const int EtimedOutFallback = ZlinkHausnumero + 16;
    private const int EhostUnreachFallback = ZlinkHausnumero + 17;
    private const int EnetResetFallback = ZlinkHausnumero + 18;
    private const int EfsmNative = ZlinkHausnumero + 51;
    private const int EnoCompatProtoNative = ZlinkHausnumero + 52;
    private const int EtermNative = ZlinkHausnumero + 53;
    private const int EmThreadNative = ZlinkHausnumero + 54;

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

    public static ErrorCode MapErrorCode(int errno)
    {
        return errno switch
        {
            0 => ErrorCode.None,
            (int)ErrorCode.EBusy => ErrorCode.EBusy,
            (int)ErrorCode.EIntr => ErrorCode.EIntr,
            (int)ErrorCode.EAgain or 35 => ErrorCode.EAgain,
            (int)ErrorCode.EBadf => ErrorCode.EBadf,
            (int)ErrorCode.ENomem => ErrorCode.ENomem,
            (int)ErrorCode.EAccess => ErrorCode.EAccess,
            (int)ErrorCode.EFault => ErrorCode.EFault,
            (int)ErrorCode.EInval => ErrorCode.EInval,
            (int)ErrorCode.ENotSock or 38 or EnotSockFallback =>
                ErrorCode.ENotSock,
            (int)ErrorCode.EMsgSize or 40 or EmsgSizeFallback =>
                ErrorCode.EMsgSize,
            (int)ErrorCode.EProtoNoSupport or 43 or EprotoNoSupportFallback =>
                ErrorCode.EProtoNoSupport,
            (int)ErrorCode.ENotSup or 45 or EnotSupFallback =>
                ErrorCode.ENotSup,
            (int)ErrorCode.EAfNoSupport or 47 or EafNoSupportFallback =>
                ErrorCode.EAfNoSupport,
            (int)ErrorCode.EAddrInUse or 48 or EaddrInUseFallback =>
                ErrorCode.EAddrInUse,
            (int)ErrorCode.EAddrNotAvail or 49 or EaddrNotAvailFallback =>
                ErrorCode.EAddrNotAvail,
            (int)ErrorCode.ENetDown or 50 or EnetDownFallback =>
                ErrorCode.ENetDown,
            (int)ErrorCode.ENetUnreach or 51 or EnetUnreachFallback =>
                ErrorCode.ENetUnreach,
            (int)ErrorCode.ENetReset or 52 or EnetResetFallback =>
                ErrorCode.ENetReset,
            (int)ErrorCode.EConnAborted or 53 or EconnAbortedFallback =>
                ErrorCode.EConnAborted,
            (int)ErrorCode.EConnReset or 54 or EconnResetFallback =>
                ErrorCode.EConnReset,
            (int)ErrorCode.ENoBufs or 55 or EnoBufsFallback =>
                ErrorCode.ENoBufs,
            (int)ErrorCode.ENotConn or 57 or EnotConnFallback =>
                ErrorCode.ENotConn,
            (int)ErrorCode.ETimedOut or 60 or EtimedOutFallback =>
                ErrorCode.ETimedOut,
            (int)ErrorCode.EConnRefused or 61 or EconnRefusedFallback =>
                ErrorCode.EConnRefused,
            (int)ErrorCode.EHostUnreach or 65 or EhostUnreachFallback =>
                ErrorCode.EHostUnreach,
            (int)ErrorCode.EShutdown => ErrorCode.EShutdown,
            (int)ErrorCode.EInProgress or 36 or EinProgressFallback =>
                ErrorCode.EInProgress,
            EfsmNative => ErrorCode.Efsm,
            EnoCompatProtoNative => ErrorCode.EnoCompatProto,
            EtermNative => ErrorCode.Eterm,
            EmThreadNative => ErrorCode.EmThread,
            _ => ErrorCode.Unknown
        };
    }

    public static bool TryMapErrorCode(int errno, out ErrorCode code)
    {
        code = MapErrorCode(errno);
        return code != ErrorCode.Unknown;
    }
}
