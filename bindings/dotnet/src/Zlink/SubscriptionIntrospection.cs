// SPDX-License-Identifier: MPL-2.0

using System;
using System.Text;
using Systems.Zlink.Native;

namespace Systems.Zlink;

internal static class SubscriptionIntrospection
{
    internal static unsafe SubscriptionEntry? At(IntPtr handle, int index)
    {
        if (index < 0)
            throw new ArgumentOutOfRangeException(nameof(index));

        nuint length = 0;
        int rc = NativeMethods.zlink_subscription_at(handle, (nuint)index,
            IntPtr.Zero, ref length, out int isPattern);
        if (rc != 0 && length == 0)
        {
            ZlinkConfigException error = ZlinkException.CreateConfigException(
                NativeMethods.zlink_errno());
            if (error.Result == ZlinkConfigException.ErrorCode.NotFound)
                return null;
            throw error;
        }

        byte[] buffer = new byte[checked((int)length)];
        if (buffer.Length == 0)
        {
            rc = NativeMethods.zlink_subscription_at(handle, (nuint)index,
                IntPtr.Zero, ref length, out isPattern);
            try
            {
                ZlinkException.ThrowConfigIfError(rc);
            }
            catch (ZlinkConfigException error)
                when (error.Result == ZlinkConfigException.ErrorCode.NotFound)
            {
                return null;
            }
            return new SubscriptionEntry(string.Empty, isPattern != 0);
        }

        fixed (byte* ptr = buffer)
        {
            rc = NativeMethods.zlink_subscription_at(handle, (nuint)index,
                (IntPtr)ptr, ref length, out isPattern);
        }
        try
        {
            ZlinkException.ThrowConfigIfError(rc);
        }
        catch (ZlinkConfigException error)
            when (error.Result == ZlinkConfigException.ErrorCode.NotFound)
        {
            return null;
        }
        return new SubscriptionEntry(
            Encoding.UTF8.GetString(buffer, 0, checked((int)length)),
            isPattern != 0);
    }
}
