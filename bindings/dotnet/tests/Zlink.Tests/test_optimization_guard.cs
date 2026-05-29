using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text.RegularExpressions;
using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_optimization_guard
{
    private static readonly string[] AggregateSymbols =
    {
        "zlink_send",
        "zlink_recv",
        "zlink_publish",
        "zlink_subscribe",
        "zlink_router_recv",
        "zlink_dealer_request",
        "zlink_router_request",
        "zlink_router_reply",
        "zlink_spot_send_channel",
        "zlink_spot_request_channel",
        "zlink_spot_request_spot",
        "zlink_spot_request_router",
        "zlink_spot_publish",
        "zlink_spot_subscribe",
        "zlink_spot_send_spot",
        "zlink_spot_reply_spot",
        "zlink_spot_reply_router",
        "zlink_spot_recv"
    };

    private static readonly string[] RequiredPartSymbols =
    {
        "zlink_send_part",
        "zlink_recv_part",
        "zlink_publish_part",
        "zlink_subscribe_part",
        "zlink_router_recv_part",
        "zlink_dealer_request_part",
        "zlink_router_request_part",
        "zlink_router_reply_part",
        "zlink_spot_publish_part",
        "zlink_spot_subscribe_part",
        "zlink_spot_request_channel_part",
        "zlink_spot_request_spot_part",
        "zlink_spot_reply_router_part"
    };

    [Fact]
    public void hot_paths_use_part_substrate_instead_of_aggregate_symbols()
    {
        string source = ReadZlinkSource();

        foreach (string symbol in RequiredPartSymbols)
            Assert.Contains(symbol, source, StringComparison.Ordinal);

        var violations = new List<string>();
        foreach (string symbol in AggregateSymbols)
        {
            if (Regex.IsMatch(source, @$"""{Regex.Escape(symbol)}"""))
                violations.Add(symbol);
            if (Regex.IsMatch(source, @$"\bNativeMethods\.{Regex.Escape(symbol)}\s*\("))
                violations.Add($"NativeMethods.{symbol}");
        }

        Assert.Empty(violations);
    }

    [Fact]
    public void runtime_source_does_not_use_dynamic_interop_workarounds()
    {
        string source = ReadZlinkSource();

        Assert.DoesNotContain("GetMethod(", source, StringComparison.Ordinal);
        Assert.DoesNotContain("GetField(", source, StringComparison.Ordinal);
        Assert.DoesNotContain("BindingFlags.NonPublic", source, StringComparison.Ordinal);
        Assert.DoesNotContain("MethodInfo.Invoke", source, StringComparison.Ordinal);
        Assert.DoesNotContain("FieldInfo.GetValue", source, StringComparison.Ordinal);
    }

    [Fact]
    public void publish_topic_cache_encodes_null_terminated_utf8_without_temp_string()
    {
        string source = ReadZlinkSource();

        Assert.Contains("PublishTopicEncoding.GetNullTerminatedUtf8", source,
            StringComparison.Ordinal);
        Assert.DoesNotContain("topic + '\\0'", source,
            StringComparison.Ordinal);
    }

    [Fact]
    public void publish_part_interop_does_not_marshal_topic_strings()
    {
        string source = ReadZlinkSource();

        Assert.Contains("zlink_publish_part_utf8", source,
            StringComparison.Ordinal);
        Assert.Contains("zlink_spot_publish_part_utf8", source,
            StringComparison.Ordinal);
        Assert.DoesNotContain("zlink_publish_part(IntPtr subject",
            source, StringComparison.Ordinal);
        Assert.DoesNotContain("zlink_spot_publish_part(IntPtr spot",
            source, StringComparison.Ordinal);
        Assert.DoesNotContain("string topicId, ref ZlinkMsg part",
            source, StringComparison.Ordinal);
    }

    [Fact]
    public void channel_part_interop_does_not_marshal_channel_name_strings()
    {
        string source = ReadZlinkSource();

        Assert.Contains("zlink_spot_send_channel_part_utf8", source,
            StringComparison.Ordinal);
        Assert.Contains("zlink_spot_request_channel_part_utf8", source,
            StringComparison.Ordinal);
        Assert.DoesNotContain("zlink_spot_send_channel_part(IntPtr spot",
            source, StringComparison.Ordinal);
        Assert.DoesNotContain("zlink_spot_request_channel_part(IntPtr spot",
            source, StringComparison.Ordinal);
    }

    [Fact]
    public void spot_dispatch_subscribe_readable_uses_cached_info()
    {
        string source = ReadZlinkSource();

        Assert.Contains("SpotDispatchInfo.SubscribeReadableSpot", source,
            StringComparison.Ordinal);
    }

    private static string ReadZlinkSource([CallerFilePath] string file = "")
    {
        string repoRoot = Path.GetFullPath(Path.Combine(
            Path.GetDirectoryName(file)!,
            "..", "..", "..", ".."));
        string sourceRoot = Path.Combine(repoRoot, "bindings", "dotnet", "src", "Zlink");
        var chunks = new List<string>();
        foreach (string path in Directory.EnumerateFiles(sourceRoot, "*.cs",
                     SearchOption.AllDirectories))
        {
            if (path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}",
                    StringComparison.Ordinal))
                continue;
            if (path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}",
                    StringComparison.Ordinal))
                continue;
            chunks.Add(File.ReadAllText(path));
        }
        return string.Join('\n', chunks);
    }
}
