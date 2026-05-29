// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;
public sealed class ZlinkSubmitException : ZlinkException
{
    public enum ErrorCode
    {
        Ok = 0,
        Backpressured = 1,
        NotConnected = 2,
        NotFound = 3,
        Terminated = 4,
        InvalidHandle = 5,
        InvalidArgument = 6,
        NotSupported = 7,
        InvalidState = 8,
        ThreadViolation = 9,
        OutOfMemory = 10,
        SeqExhausted = 11,
        InternalError = 12,
        NotAdmitted = 13
    }

    public ZlinkSubmitException(ErrorCode result)
        : this(result, 0)
    {
    }

    public ZlinkSubmitException(ErrorCode result, int internalErrno)
        : base((int)result, internalErrno)
    {
        Result = result;
    }

    internal ZlinkSubmitException(SubmitResult result)
        : this((ErrorCode)result, 0)
    {
    }

    internal ZlinkSubmitException(SubmitResult result, int internalErrno)
        : this((ErrorCode)result, internalErrno)
    {
    }

    public ErrorCode Result { get; }
}

public sealed class ZlinkRequestException : ZlinkException
{
    public enum ErrorCode
    {
        Ok = 0,
        TimedOut = 101,
        NotFound = 102,
        Terminated = 103,
        ProtocolError = 104,
        InternalError = 105,
        Rejected = 106,
        Conflict = 107,
        Busy = 108,
        NotConnected = 109,
        InvalidArgument = 110,
        InvalidState = 111,
        NotSupported = 112
    }

    public ZlinkRequestException(ErrorCode result)
        : this(result, 0)
    {
    }

    public ZlinkRequestException(ErrorCode result, int internalErrno)
        : base((int)result, internalErrno)
    {
        Result = result;
    }

    internal ZlinkRequestException(RequestResult result)
        : this((ErrorCode)result, 0)
    {
    }

    internal ZlinkRequestException(RequestResult result, int internalErrno)
        : this((ErrorCode)result, internalErrno)
    {
    }

    public ErrorCode Result { get; }
}

public sealed class ZlinkRecvException : ZlinkException
{
    public enum ErrorCode
    {
        Ok = 0,
        NoData = 201,
        Busy = 202,
        Terminated = 203,
        InvalidHandle = 204,
        NotSupported = 205,
        InternalError = 206
    }

    public ZlinkRecvException(ErrorCode result)
        : this(result, 0)
    {
    }

    public ZlinkRecvException(ErrorCode result, int internalErrno)
        : base((int)result, internalErrno)
    {
        Result = result;
    }

    internal ZlinkRecvException(RecvResult result)
        : this((ErrorCode)result, 0)
    {
    }

    internal ZlinkRecvException(RecvResult result, int internalErrno)
        : this((ErrorCode)result, internalErrno)
    {
    }

    public ErrorCode Result { get; }
}

public sealed class ZlinkHandlerException : ZlinkException
{
    public enum ErrorCode
    {
        Ok = 0,
        InvalidArgument = 301,
        Busy = 302,
        NotSupported = 303,
        Deadlock = 304,
        InvalidHandle = 305,
        InternalError = 306
    }

    public ZlinkHandlerException(ErrorCode result)
        : this(result, 0)
    {
    }

    public ZlinkHandlerException(ErrorCode result, int internalErrno)
        : base((int)result, internalErrno)
    {
        Result = result;
    }

    internal ZlinkHandlerException(HandlerResult result, int internalErrno)
        : this((ErrorCode)result, internalErrno)
    {
    }

    internal ZlinkHandlerException(HandlerResult result)
        : this((ErrorCode)result, 0)
    {
    }

    public ErrorCode Result { get; }
}

public sealed class ZlinkCloseException : ZlinkException
{
    public enum ErrorCode
    {
        Ok = 0,
        Busy = 401,
        Shutdown = 402,
        InvalidHandle = 403,
        InternalError = 404
    }

    public ZlinkCloseException(ErrorCode result)
        : this(result, 0)
    {
    }

    public ZlinkCloseException(ErrorCode result, int internalErrno)
        : base((int)result, internalErrno)
    {
        Result = result;
    }

    internal ZlinkCloseException(CloseResult result, int internalErrno)
        : this((ErrorCode)result, internalErrno)
    {
    }

    internal ZlinkCloseException(CloseResult result)
        : this((ErrorCode)result, 0)
    {
    }

    public ErrorCode Result { get; }
}

public sealed class ZlinkBindException : ZlinkException
{
    public enum ErrorCode
    {
        Ok = 0,
        InvalidArgument = 501,
        AddrInUse = 502,
        NotSupported = 503,
        InvalidHandle = 504,
        InternalError = 505
    }

    public ZlinkBindException(ErrorCode result)
        : this(result, 0)
    {
    }

    public ZlinkBindException(ErrorCode result, int internalErrno)
        : base((int)result, internalErrno)
    {
        Result = result;
    }

    internal ZlinkBindException(BindResult result, int internalErrno)
        : this((ErrorCode)result, internalErrno)
    {
    }

    internal ZlinkBindException(BindResult result)
        : this((ErrorCode)result, 0)
    {
    }

    public ErrorCode Result { get; }
}

public sealed class ZlinkConnectException : ZlinkException
{
    public enum ErrorCode
    {
        Ok = 0,
        InvalidArgument = 601,
        NotSupported = 602,
        InvalidHandle = 603,
        InternalError = 604,
        NotFound = 605,
        Conflict = 606,
        Busy = 607
    }

    public ZlinkConnectException(ErrorCode result)
        : this(result, 0)
    {
    }

    public ZlinkConnectException(ErrorCode result, int internalErrno)
        : base((int)result, internalErrno)
    {
        Result = result;
    }

    internal ZlinkConnectException(ConnectResult result, int internalErrno)
        : this((ErrorCode)result, internalErrno)
    {
    }

    internal ZlinkConnectException(ConnectResult result)
        : this((ErrorCode)result, 0)
    {
    }

    public ErrorCode Result { get; }
}

public sealed class ZlinkConfigException : ZlinkException
{
    public enum ErrorCode
    {
        Ok = 0,
        InvalidHandle = 701,
        InvalidArgument = 702,
        NotSupported = 703,
        InternalError = 704,
        InvalidState = 705,
        NotFound = 706
    }

    public ZlinkConfigException(ErrorCode result)
        : this(result, 0)
    {
    }

    public ZlinkConfigException(ErrorCode result, int internalErrno)
        : base((int)result, internalErrno)
    {
        Result = result;
    }

    internal ZlinkConfigException(ConfigResult result, int internalErrno)
        : this((ErrorCode)result, internalErrno)
    {
    }

    internal ZlinkConfigException(ConfigResult result)
        : this((ErrorCode)result, 0)
    {
    }

    public ErrorCode Result { get; }
}
