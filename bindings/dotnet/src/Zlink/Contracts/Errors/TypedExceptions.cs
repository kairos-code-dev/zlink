// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;
/// <summary>
/// Represents zlink submit exception.
/// </summary>
public sealed class ZlinkSubmitException : ZlinkException
{
    /// <summary>
    /// Defines error code values.
    /// </summary>
    public enum ErrorCode
    {
        /// <summary>
        /// Indicates the successful error code.
        /// </summary>
        Ok = 0,
        /// <summary>
        /// Indicates the backpressured error code.
        /// </summary>
        Backpressured = 1,
        /// <summary>
        /// Indicates the not connected error code.
        /// </summary>
        NotConnected = 2,
        /// <summary>
        /// Indicates the not found error code.
        /// </summary>
        NotFound = 3,
        /// <summary>
        /// Indicates the terminated error code.
        /// </summary>
        Terminated = 4,
        /// <summary>
        /// Indicates the invalid handle error code.
        /// </summary>
        InvalidHandle = 5,
        /// <summary>
        /// Indicates the invalid argument error code.
        /// </summary>
        InvalidArgument = 6,
        /// <summary>
        /// Indicates the not supported error code.
        /// </summary>
        NotSupported = 7,
        /// <summary>
        /// Indicates the invalid state error code.
        /// </summary>
        InvalidState = 8,
        /// <summary>
        /// Indicates the thread violation error code.
        /// </summary>
        ThreadViolation = 9,
        /// <summary>
        /// Indicates the out of memory error code.
        /// </summary>
        OutOfMemory = 10,
        /// <summary>
        /// Indicates the seq exhausted error code.
        /// </summary>
        SeqExhausted = 11,
        /// <summary>
        /// Indicates the internal error error code.
        /// </summary>
        InternalError = 12,
        /// <summary>
        /// Indicates the not admitted error code.
        /// </summary>
        NotAdmitted = 13
    }

    /// <summary>
    /// Creates a zlink submit exception instance.
    /// </summary>
    public ZlinkSubmitException(ErrorCode result)
        : this(result, 0)
    {
    }

    /// <summary>
    /// Creates a zlink submit exception instance.
    /// </summary>
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

    /// <summary>
    /// Gets the result.
    /// </summary>
    public ErrorCode Result { get; }
}

/// <summary>
/// Represents zlink request exception.
/// </summary>
public sealed class ZlinkRequestException : ZlinkException
{
    /// <summary>
    /// Defines error code values.
    /// </summary>
    public enum ErrorCode
    {
        /// <summary>
        /// Indicates the successful error code.
        /// </summary>
        Ok = 0,
        /// <summary>
        /// Indicates the timed out error code.
        /// </summary>
        TimedOut = 101,
        /// <summary>
        /// Indicates the not found error code.
        /// </summary>
        NotFound = 102,
        /// <summary>
        /// Indicates the terminated error code.
        /// </summary>
        Terminated = 103,
        /// <summary>
        /// Indicates the protocol error error code.
        /// </summary>
        ProtocolError = 104,
        /// <summary>
        /// Indicates the internal error error code.
        /// </summary>
        InternalError = 105,
        /// <summary>
        /// Indicates the rejected error code.
        /// </summary>
        Rejected = 106,
        /// <summary>
        /// Indicates the conflict error code.
        /// </summary>
        Conflict = 107,
        /// <summary>
        /// Indicates the busy error code.
        /// </summary>
        Busy = 108,
        /// <summary>
        /// Indicates the not connected error code.
        /// </summary>
        NotConnected = 109,
        /// <summary>
        /// Indicates the invalid argument error code.
        /// </summary>
        InvalidArgument = 110,
        /// <summary>
        /// Indicates the invalid state error code.
        /// </summary>
        InvalidState = 111,
        /// <summary>
        /// Indicates the not supported error code.
        /// </summary>
        NotSupported = 112
    }

    /// <summary>
    /// Creates a zlink request exception instance.
    /// </summary>
    public ZlinkRequestException(ErrorCode result)
        : this(result, 0)
    {
    }

    /// <summary>
    /// Creates a zlink request exception instance.
    /// </summary>
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

    /// <summary>
    /// Gets the result.
    /// </summary>
    public ErrorCode Result { get; }
}

/// <summary>
/// Represents zlink receive exception.
/// </summary>
public sealed class ZlinkRecvException : ZlinkException
{
    /// <summary>
    /// Defines error code values.
    /// </summary>
    public enum ErrorCode
    {
        /// <summary>
        /// Indicates the successful error code.
        /// </summary>
        Ok = 0,
        /// <summary>
        /// Indicates the no data error code.
        /// </summary>
        NoData = 201,
        /// <summary>
        /// Indicates the busy error code.
        /// </summary>
        Busy = 202,
        /// <summary>
        /// Indicates the terminated error code.
        /// </summary>
        Terminated = 203,
        /// <summary>
        /// Indicates the invalid handle error code.
        /// </summary>
        InvalidHandle = 204,
        /// <summary>
        /// Indicates the not supported error code.
        /// </summary>
        NotSupported = 205,
        /// <summary>
        /// Indicates the internal error error code.
        /// </summary>
        InternalError = 206
    }

    /// <summary>
    /// Creates a zlink receive exception instance.
    /// </summary>
    public ZlinkRecvException(ErrorCode result)
        : this(result, 0)
    {
    }

    /// <summary>
    /// Creates a zlink receive exception instance.
    /// </summary>
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

    /// <summary>
    /// Gets the result.
    /// </summary>
    public ErrorCode Result { get; }
}

/// <summary>
/// Represents zlink handler exception.
/// </summary>
public sealed class ZlinkHandlerException : ZlinkException
{
    /// <summary>
    /// Defines error code values.
    /// </summary>
    public enum ErrorCode
    {
        /// <summary>
        /// Indicates the successful error code.
        /// </summary>
        Ok = 0,
        /// <summary>
        /// Indicates the invalid argument error code.
        /// </summary>
        InvalidArgument = 301,
        /// <summary>
        /// Indicates the busy error code.
        /// </summary>
        Busy = 302,
        /// <summary>
        /// Indicates the not supported error code.
        /// </summary>
        NotSupported = 303,
        /// <summary>
        /// Indicates the deadlock error code.
        /// </summary>
        Deadlock = 304,
        /// <summary>
        /// Indicates the invalid handle error code.
        /// </summary>
        InvalidHandle = 305,
        /// <summary>
        /// Indicates the internal error error code.
        /// </summary>
        InternalError = 306
    }

    /// <summary>
    /// Creates a zlink handler exception instance.
    /// </summary>
    public ZlinkHandlerException(ErrorCode result)
        : this(result, 0)
    {
    }

    /// <summary>
    /// Creates a zlink handler exception instance.
    /// </summary>
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

    /// <summary>
    /// Gets the result.
    /// </summary>
    public ErrorCode Result { get; }
}

/// <summary>
/// Represents zlink close exception.
/// </summary>
public sealed class ZlinkCloseException : ZlinkException
{
    /// <summary>
    /// Defines error code values.
    /// </summary>
    public enum ErrorCode
    {
        /// <summary>
        /// Indicates the successful error code.
        /// </summary>
        Ok = 0,
        /// <summary>
        /// Indicates the busy error code.
        /// </summary>
        Busy = 401,
        /// <summary>
        /// Indicates the shutdown error code.
        /// </summary>
        Shutdown = 402,
        /// <summary>
        /// Indicates the invalid handle error code.
        /// </summary>
        InvalidHandle = 403,
        /// <summary>
        /// Indicates the internal error error code.
        /// </summary>
        InternalError = 404
    }

    /// <summary>
    /// Creates a zlink close exception instance.
    /// </summary>
    public ZlinkCloseException(ErrorCode result)
        : this(result, 0)
    {
    }

    /// <summary>
    /// Creates a zlink close exception instance.
    /// </summary>
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

    /// <summary>
    /// Gets the result.
    /// </summary>
    public ErrorCode Result { get; }
}

/// <summary>
/// Represents zlink bind exception.
/// </summary>
public sealed class ZlinkBindException : ZlinkException
{
    /// <summary>
    /// Defines error code values.
    /// </summary>
    public enum ErrorCode
    {
        /// <summary>
        /// Indicates the successful error code.
        /// </summary>
        Ok = 0,
        /// <summary>
        /// Indicates the invalid argument error code.
        /// </summary>
        InvalidArgument = 501,
        /// <summary>
        /// Indicates the addr in use error code.
        /// </summary>
        AddrInUse = 502,
        /// <summary>
        /// Indicates the not supported error code.
        /// </summary>
        NotSupported = 503,
        /// <summary>
        /// Indicates the invalid handle error code.
        /// </summary>
        InvalidHandle = 504,
        /// <summary>
        /// Indicates the internal error error code.
        /// </summary>
        InternalError = 505
    }

    /// <summary>
    /// Creates a zlink bind exception instance.
    /// </summary>
    public ZlinkBindException(ErrorCode result)
        : this(result, 0)
    {
    }

    /// <summary>
    /// Creates a zlink bind exception instance.
    /// </summary>
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

    /// <summary>
    /// Gets the result.
    /// </summary>
    public ErrorCode Result { get; }
}

/// <summary>
/// Represents zlink connect exception.
/// </summary>
public sealed class ZlinkConnectException : ZlinkException
{
    /// <summary>
    /// Defines error code values.
    /// </summary>
    public enum ErrorCode
    {
        /// <summary>
        /// Indicates the successful error code.
        /// </summary>
        Ok = 0,
        /// <summary>
        /// Indicates the invalid argument error code.
        /// </summary>
        InvalidArgument = 601,
        /// <summary>
        /// Indicates the not supported error code.
        /// </summary>
        NotSupported = 602,
        /// <summary>
        /// Indicates the invalid handle error code.
        /// </summary>
        InvalidHandle = 603,
        /// <summary>
        /// Indicates the internal error error code.
        /// </summary>
        InternalError = 604,
        /// <summary>
        /// Indicates the not found error code.
        /// </summary>
        NotFound = 605,
        /// <summary>
        /// Indicates the conflict error code.
        /// </summary>
        Conflict = 606,
        /// <summary>
        /// Indicates the busy error code.
        /// </summary>
        Busy = 607
    }

    /// <summary>
    /// Creates a zlink connect exception instance.
    /// </summary>
    public ZlinkConnectException(ErrorCode result)
        : this(result, 0)
    {
    }

    /// <summary>
    /// Creates a zlink connect exception instance.
    /// </summary>
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

    /// <summary>
    /// Gets the result.
    /// </summary>
    public ErrorCode Result { get; }
}

/// <summary>
/// Represents zlink config exception.
/// </summary>
public sealed class ZlinkConfigException : ZlinkException
{
    /// <summary>
    /// Defines error code values.
    /// </summary>
    public enum ErrorCode
    {
        /// <summary>
        /// Indicates the successful error code.
        /// </summary>
        Ok = 0,
        /// <summary>
        /// Indicates the invalid handle error code.
        /// </summary>
        InvalidHandle = 701,
        /// <summary>
        /// Indicates the invalid argument error code.
        /// </summary>
        InvalidArgument = 702,
        /// <summary>
        /// Indicates the not supported error code.
        /// </summary>
        NotSupported = 703,
        /// <summary>
        /// Indicates the internal error error code.
        /// </summary>
        InternalError = 704,
        /// <summary>
        /// Indicates the invalid state error code.
        /// </summary>
        InvalidState = 705,
        /// <summary>
        /// Indicates the not found error code.
        /// </summary>
        NotFound = 706
    }

    /// <summary>
    /// Creates a zlink config exception instance.
    /// </summary>
    public ZlinkConfigException(ErrorCode result)
        : this(result, 0)
    {
    }

    /// <summary>
    /// Creates a zlink config exception instance.
    /// </summary>
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

    /// <summary>
    /// Gets the result.
    /// </summary>
    public ErrorCode Result { get; }
}
