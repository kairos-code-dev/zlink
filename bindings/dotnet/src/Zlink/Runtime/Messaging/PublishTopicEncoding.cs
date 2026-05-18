// SPDX-License-Identifier: MPL-2.0

using System.Text;

namespace Systems.Zlink;

internal static class PublishTopicEncoding
{
    internal static byte[] GetNullTerminatedUtf8(string topic)
    {
        int byteCount = Encoding.UTF8.GetByteCount(topic);
        byte[] encoded = new byte[byteCount + 1];
        int written = Encoding.UTF8.GetBytes(topic.AsSpan(),
            encoded.AsSpan(0, byteCount));
        encoded[written] = 0;
        return encoded;
    }
}
