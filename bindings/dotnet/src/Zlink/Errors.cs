// SPDX-License-Identifier: MPL-2.0

using System;
using System.Globalization;
using System.Runtime.InteropServices;
using Zlink.Native;

namespace Zlink;

public abstract class ZlinkException : Exception
{
    protected ZlinkException(int code)
        : this(code, 0)
    {
    }

    protected ZlinkException(int code, int internalErrno)
        : base(BuildMessage(code, internalErrno))
    {
        Code = code;
        InternalErrno = internalErrno;
    }

    public int Code { get; }

    public int InternalErrno { get; }

    public override string Message => base.Message;

    internal static ZlinkException FromLastError()
    {
        int errno = NativeMethods.zlink_errno();
        return new LegacyZlinkException(errno);
    }

    internal static void ThrowIfError(int rc)
    {
        if (rc != 0)
            throw FromLastError();
    }

    internal static void ThrowConfigIfError(int rc)
    {
        if (rc != 0)
            throw CreateConfigException(NativeMethods.zlink_errno());
    }

    internal static void ThrowBindIfError(int rc)
    {
        if (rc != 0)
            throw CreateBindException(NativeMethods.zlink_errno());
    }

    internal static void ThrowConnectIfError(int rc)
    {
        if (rc != 0)
            throw CreateConnectException(NativeMethods.zlink_errno());
    }

    internal static void ThrowRecvIfError(int rc)
    {
        if (rc != 0)
            throw CreateRecvException(NativeMethods.zlink_errno());
    }

    internal static void ThrowSubmitIfError(int rc)
    {
        if (rc != 0)
            throw CreateSubmitException(NativeMethods.zlink_errno());
    }

    internal static void ThrowHandlerIfError(int rc)
    {
        if (rc != 0)
            throw CreateHandlerException(NativeMethods.zlink_errno());
    }

    internal static void ThrowCloseIfError(int rc)
    {
        if (rc != 0)
            throw CreateCloseException(NativeMethods.zlink_errno());
    }

    internal static ErrorCode MapErrorCode(int errno)
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

    internal static bool TryMapErrorCode(int errno, out ErrorCode code)
    {
        code = MapErrorCode(errno);
        return code != ErrorCode.Unknown;
    }

    internal static ZlinkSubmitException CreateSubmitException(int errno)
        => new(MapSubmitResult(errno), errno);

    internal static ZlinkRequestException CreateRequestException(int errno)
        => new(MapRequestResult(errno), errno);

    internal static ZlinkRecvException CreateRecvException(int errno)
        => new(MapRecvResult(errno), errno);

    internal static ZlinkHandlerException CreateHandlerException(int errno)
        => new(MapHandlerResult(errno), errno);

    internal static ZlinkCloseException CreateCloseException(int errno)
        => new(MapCloseResult(errno), errno);

    internal static ZlinkBindException CreateBindException(int errno)
        => new(MapBindResult(errno), errno);

    internal static ZlinkConnectException CreateConnectException(int errno)
        => new(MapConnectResult(errno), errno);

    internal static ZlinkConfigException CreateConfigException(int errno)
        => new(MapConfigResult(errno), errno);

    private static string BuildMessage(int code, int internalErrno)
    {
        return internalErrno == 0
            ? $"zlink error code {code}"
            : string.Create(CultureInfo.InvariantCulture, $"zlink error code {code} (errno {internalErrno})");
    }

    private static SubmitResult MapSubmitResult(int errno)
    {
        return errno switch
        {
            0 => SubmitResult.Ok,
            11 or 35 or 10035 => SubmitResult.Backpressured,
            13 => SubmitResult.NotAdmitted,
            107 or 113 or 111 or 110 or 10057 or 10060 or 10065 =>
                SubmitResult.NotConnected,
            2 or 3 => SubmitResult.NotFound,
            156384765 => SubmitResult.Terminated,
            9 or 88 => SubmitResult.InvalidHandle,
            22 => SubmitResult.InvalidArgument,
            95 or 93 or 97 => SubmitResult.NotSupported,
            16 => SubmitResult.InvalidState,
            156384766 => SubmitResult.ThreadViolation,
            12 or 105 => SubmitResult.OutOfMemory,
            _ => SubmitResult.InternalError
        };
    }

    private static RequestResult MapRequestResult(int errno)
    {
        return errno switch
        {
            0 => RequestResult.Ok,
            60 or 110 or 10060 => RequestResult.TimedOut,
            3 or 2 => RequestResult.NotFound,
            156384765 => RequestResult.Terminated,
            104 => RequestResult.ProtocolError,
            107 or 113 or 111 or 10057 or 10065 =>
                RequestResult.NotConnected,
            13 => RequestResult.NotAdmitted,
            1 => RequestResult.Rejected,
            17 => RequestResult.Conflict,
            22 => RequestResult.InvalidArgument,
            16 => RequestResult.InvalidState,
            156384766 => RequestResult.ThreadViolation,
            12 or 105 => RequestResult.InternalError,
            _ => RequestResult.InternalError
        };
    }

    private static RecvResult MapRecvResult(int errno)
    {
        return errno switch
        {
            0 => RecvResult.Ok,
            11 or 35 or 10035 => RecvResult.NoData,
            16 => RecvResult.Busy,
            156384765 => RecvResult.Terminated,
            9 or 88 => RecvResult.InvalidHandle,
            95 or 93 or 97 => RecvResult.NotSupported,
            _ => RecvResult.NoData
        };
    }

    private static HandlerResult MapHandlerResult(int errno)
    {
        return errno switch
        {
            0 => HandlerResult.Ok,
            22 => HandlerResult.InvalidArgument,
            16 => HandlerResult.Busy,
            95 or 93 or 97 => HandlerResult.NotSupported,
            35 => HandlerResult.Deadlock,
            9 or 88 => HandlerResult.InvalidHandle,
            _ => HandlerResult.InvalidArgument
        };
    }

    private static CloseResult MapCloseResult(int errno)
    {
        return errno switch
        {
            0 => CloseResult.Ok,
            16 => CloseResult.Busy,
            108 => CloseResult.Shutdown,
            9 or 88 => CloseResult.InvalidHandle,
            _ => CloseResult.InvalidHandle
        };
    }

    private static BindResult MapBindResult(int errno)
    {
        return errno switch
        {
            0 => BindResult.Ok,
            22 => BindResult.InvalidArgument,
            98 => BindResult.AddrInUse,
            95 or 93 or 97 => BindResult.NotSupported,
            9 or 88 => BindResult.InvalidHandle,
            _ => BindResult.InvalidArgument
        };
    }

    private static ConnectResult MapConnectResult(int errno)
    {
        return errno switch
        {
            0 => ConnectResult.Ok,
            22 => ConnectResult.InvalidArgument,
            95 or 93 or 97 => ConnectResult.NotSupported,
            9 or 88 => ConnectResult.InvalidHandle,
            _ => ConnectResult.InvalidArgument
        };
    }

    private static ConfigResult MapConfigResult(int errno)
    {
        return errno switch
        {
            0 => ConfigResult.Ok,
            9 or 88 => ConfigResult.InvalidHandle,
            22 => ConfigResult.InvalidArgument,
            95 or 93 or 97 => ConfigResult.NotSupported,
            _ => ConfigResult.InvalidArgument
        };
    }

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

    private sealed class LegacyZlinkException : ZlinkException
    {
        internal LegacyZlinkException(int errno)
            : base(MapErrorCode(errno) == ErrorCode.Unknown ? errno : (int)MapErrorCode(errno), errno)
        {
        }
    }
}

public sealed class ZlinkSubmitException : ZlinkException
{
    public ZlinkSubmitException(SubmitResult result)
        : this(result, 0)
    {
    }

    public ZlinkSubmitException(SubmitResult result, int internalErrno)
        : base((int)result, internalErrno)
    {
        Result = result;
    }

    public SubmitResult Result { get; }
}

public sealed class ZlinkRequestException : ZlinkException
{
    public ZlinkRequestException(RequestResult result)
        : this(result, 0)
    {
    }

    public ZlinkRequestException(RequestResult result, int internalErrno)
        : base((int)result, internalErrno)
    {
        Result = result;
    }

    public RequestResult Result { get; }
}

public sealed class ZlinkRecvException : ZlinkException
{
    public ZlinkRecvException(RecvResult result)
        : this(result, 0)
    {
    }

    public ZlinkRecvException(RecvResult result, int internalErrno)
        : base((int)result, internalErrno)
    {
        Result = result;
    }

    public RecvResult Result { get; }
}

public sealed class ZlinkHandlerException : ZlinkException
{
    public ZlinkHandlerException(HandlerResult result)
        : this(result, 0)
    {
    }

    public ZlinkHandlerException(HandlerResult result, int internalErrno)
        : base((int)result, internalErrno)
    {
        Result = result;
    }

    public HandlerResult Result { get; }
}

public sealed class ZlinkCloseException : ZlinkException
{
    public ZlinkCloseException(CloseResult result)
        : this(result, 0)
    {
    }

    public ZlinkCloseException(CloseResult result, int internalErrno)
        : base((int)result, internalErrno)
    {
        Result = result;
    }

    public CloseResult Result { get; }
}

public sealed class ZlinkBindException : ZlinkException
{
    public ZlinkBindException(BindResult result)
        : this(result, 0)
    {
    }

    public ZlinkBindException(BindResult result, int internalErrno)
        : base((int)result, internalErrno)
    {
        Result = result;
    }

    public BindResult Result { get; }
}

public sealed class ZlinkConnectException : ZlinkException
{
    public ZlinkConnectException(ConnectResult result)
        : this(result, 0)
    {
    }

    public ZlinkConnectException(ConnectResult result, int internalErrno)
        : base((int)result, internalErrno)
    {
        Result = result;
    }

    public ConnectResult Result { get; }
}

public sealed class ZlinkConfigException : ZlinkException
{
    public ZlinkConfigException(ConfigResult result)
        : this(result, 0)
    {
    }

    public ZlinkConfigException(ConfigResult result, int internalErrno)
        : base((int)result, internalErrno)
    {
        Result = result;
    }

    public ConfigResult Result { get; }
}
