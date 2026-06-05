// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Base class for all exceptions thrown by the zlink bindings.
/// </summary>
public abstract partial class ZlinkException : Exception
{
    /// <summary>
    /// Creates an exception with the given zlink code and no underlying errno.
    /// </summary>
    protected ZlinkException(int code)
        : this(code, 0)
    {
    }

    /// <summary>
    /// Creates an exception with the given zlink code and the native errno that
    /// produced it.
    /// </summary>
    protected ZlinkException(int code, int internalErrno)
        : base(BuildMessage(code, internalErrno))
    {
        Code = code;
        InternalErrno = internalErrno;
    }

    /// <summary>
    /// Gets the zlink result code that classifies this failure.
    /// </summary>
    public int Code { get; }

    /// <summary>
    /// Gets the native errno that produced this failure, or 0 when none.
    /// </summary>
    public int InternalErrno { get; }

    /// <summary>
    /// Gets the formatted error message.
    /// </summary>
    public override string Message => base.Message;
}
