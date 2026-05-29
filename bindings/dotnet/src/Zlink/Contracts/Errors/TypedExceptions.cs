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
        /// Represents the Ok value.
        /// </summary>
        Ok = 0,
        /// <summary>
        /// Represents the Backpressured value.
        /// </summary>
        Backpressured = 1,
        /// <summary>
        /// Represents the NotConnected value.
        /// </summary>
        NotConnected = 2,
        /// <summary>
        /// Represents the NotFound value.
        /// </summary>
        NotFound = 3,
        /// <summary>
        /// Represents the Terminated value.
        /// </summary>
        Terminated = 4,
        /// <summary>
        /// Represents the InvalidHandle value.
        /// </summary>
        InvalidHandle = 5,
        /// <summary>
        /// Represents the InvalidArgument value.
        /// </summary>
        InvalidArgument = 6,
        /// <summary>
        /// Represents the NotSupported value.
        /// </summary>
        NotSupported = 7,
        /// <summary>
        /// Represents the InvalidState value.
        /// </summary>
        InvalidState = 8,
        /// <summary>
        /// Represents the ThreadViolation value.
        /// </summary>
        ThreadViolation = 9,
        /// <summary>
        /// Represents the OutOfMemory value.
        /// </summary>
        OutOfMemory = 10,
        /// <summary>
        /// Represents the SeqExhausted value.
        /// </summary>
        SeqExhausted = 11,
        /// <summary>
        /// Represents the InternalError value.
        /// </summary>
        InternalError = 12,
        /// <summary>
        /// Represents the NotAdmitted value.
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
    /// Gets or sets the result.
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
        /// Represents the Ok value.
        /// </summary>
        Ok = 0,
        /// <summary>
        /// Represents the TimedOut value.
        /// </summary>
        TimedOut = 101,
        /// <summary>
        /// Represents the NotFound value.
        /// </summary>
        NotFound = 102,
        /// <summary>
        /// Represents the Terminated value.
        /// </summary>
        Terminated = 103,
        /// <summary>
        /// Represents the ProtocolError value.
        /// </summary>
        ProtocolError = 104,
        /// <summary>
        /// Represents the InternalError value.
        /// </summary>
        InternalError = 105,
        /// <summary>
        /// Represents the Rejected value.
        /// </summary>
        Rejected = 106,
        /// <summary>
        /// Represents the Conflict value.
        /// </summary>
        Conflict = 107,
        /// <summary>
        /// Represents the Busy value.
        /// </summary>
        Busy = 108,
        /// <summary>
        /// Represents the NotConnected value.
        /// </summary>
        NotConnected = 109,
        /// <summary>
        /// Represents the InvalidArgument value.
        /// </summary>
        InvalidArgument = 110,
        /// <summary>
        /// Represents the InvalidState value.
        /// </summary>
        InvalidState = 111,
        /// <summary>
        /// Represents the NotSupported value.
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
    /// Gets or sets the result.
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
        /// Represents the Ok value.
        /// </summary>
        Ok = 0,
        /// <summary>
        /// Represents the NoData value.
        /// </summary>
        NoData = 201,
        /// <summary>
        /// Represents the Busy value.
        /// </summary>
        Busy = 202,
        /// <summary>
        /// Represents the Terminated value.
        /// </summary>
        Terminated = 203,
        /// <summary>
        /// Represents the InvalidHandle value.
        /// </summary>
        InvalidHandle = 204,
        /// <summary>
        /// Represents the NotSupported value.
        /// </summary>
        NotSupported = 205,
        /// <summary>
        /// Represents the InternalError value.
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
    /// Gets or sets the result.
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
        /// Represents the Ok value.
        /// </summary>
        Ok = 0,
        /// <summary>
        /// Represents the InvalidArgument value.
        /// </summary>
        InvalidArgument = 301,
        /// <summary>
        /// Represents the Busy value.
        /// </summary>
        Busy = 302,
        /// <summary>
        /// Represents the NotSupported value.
        /// </summary>
        NotSupported = 303,
        /// <summary>
        /// Represents the Deadlock value.
        /// </summary>
        Deadlock = 304,
        /// <summary>
        /// Represents the InvalidHandle value.
        /// </summary>
        InvalidHandle = 305,
        /// <summary>
        /// Represents the InternalError value.
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
    /// Gets or sets the result.
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
        /// Represents the Ok value.
        /// </summary>
        Ok = 0,
        /// <summary>
        /// Represents the Busy value.
        /// </summary>
        Busy = 401,
        /// <summary>
        /// Represents the Shutdown value.
        /// </summary>
        Shutdown = 402,
        /// <summary>
        /// Represents the InvalidHandle value.
        /// </summary>
        InvalidHandle = 403,
        /// <summary>
        /// Represents the InternalError value.
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
    /// Gets or sets the result.
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
        /// Represents the Ok value.
        /// </summary>
        Ok = 0,
        /// <summary>
        /// Represents the InvalidArgument value.
        /// </summary>
        InvalidArgument = 501,
        /// <summary>
        /// Represents the AddrInUse value.
        /// </summary>
        AddrInUse = 502,
        /// <summary>
        /// Represents the NotSupported value.
        /// </summary>
        NotSupported = 503,
        /// <summary>
        /// Represents the InvalidHandle value.
        /// </summary>
        InvalidHandle = 504,
        /// <summary>
        /// Represents the InternalError value.
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
    /// Gets or sets the result.
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
        /// Represents the Ok value.
        /// </summary>
        Ok = 0,
        /// <summary>
        /// Represents the InvalidArgument value.
        /// </summary>
        InvalidArgument = 601,
        /// <summary>
        /// Represents the NotSupported value.
        /// </summary>
        NotSupported = 602,
        /// <summary>
        /// Represents the InvalidHandle value.
        /// </summary>
        InvalidHandle = 603,
        /// <summary>
        /// Represents the InternalError value.
        /// </summary>
        InternalError = 604,
        /// <summary>
        /// Represents the NotFound value.
        /// </summary>
        NotFound = 605,
        /// <summary>
        /// Represents the Conflict value.
        /// </summary>
        Conflict = 606,
        /// <summary>
        /// Represents the Busy value.
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
    /// Gets or sets the result.
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
        /// Represents the Ok value.
        /// </summary>
        Ok = 0,
        /// <summary>
        /// Represents the InvalidHandle value.
        /// </summary>
        InvalidHandle = 701,
        /// <summary>
        /// Represents the InvalidArgument value.
        /// </summary>
        InvalidArgument = 702,
        /// <summary>
        /// Represents the NotSupported value.
        /// </summary>
        NotSupported = 703,
        /// <summary>
        /// Represents the InternalError value.
        /// </summary>
        InternalError = 704,
        /// <summary>
        /// Represents the InvalidState value.
        /// </summary>
        InvalidState = 705,
        /// <summary>
        /// Represents the NotFound value.
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
    /// Gets or sets the result.
    /// </summary>
    public ErrorCode Result { get; }
}
