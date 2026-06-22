using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text.RegularExpressions;
using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_optimization_guard
{
    private static readonly Regex PublicTypeDeclaration = new(
        @"^\s*public\s+(?:readonly\s+|sealed\s+|abstract\s+|static\s+|partial\s+|ref\s+)*" +
        @"(?:class|struct|record|interface|enum|delegate)\s+([A-Za-z_][A-Za-z0-9_]*)",
        RegexOptions.Multiline | RegexOptions.Compiled);

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

    [Fact]
    public void topic_message_writable_buffer_receive_does_not_reset_topic_twice()
    {
        string source = ReadZlinkSource();

        Assert.Contains("ResetForReuse(resetTopic: false)", source,
            StringComparison.Ordinal);
        Assert.Contains("avoiding a transient empty", source,
            StringComparison.Ordinal);
    }

    [Fact]
    public void samples_and_perf_use_only_public_binding_contracts()
    {
        string source = ReadSampleAndPerfSource();
        string[] forbidden =
        {
            "Systems.Zlink.Native",
            "Systems.Zlink.Sockets.Internal",
            "Systems.Zlink.Runtime",
            "NativeMethods",
            "NativeLibraryLoader",
            "DllImport(",
            "LibraryImport(",
            "BindingFlags.NonPublic",
            "InternalsVisibleTo",
            "FromNative",
            "MoveFromNative"
        };

        var violations = new List<string>();
        foreach (string token in forbidden)
        {
            if (source.Contains(token, StringComparison.Ordinal))
                violations.Add(token);
        }

        Assert.Empty(violations);
    }

    [Fact]
    public void internal_runtime_namespaces_stay_under_runtime()
    {
        string source = ReadZlinkSource();
        string[] forbidden =
        {
            "namespace Systems.Zlink.Native",
            "namespace Systems.Zlink.Sockets.Internal",
            "using Systems.Zlink.Native",
            "using Systems.Zlink.Sockets.Internal"
        };

        var violations = new List<string>();
        foreach (string token in forbidden)
        {
            if (source.Contains(token, StringComparison.Ordinal))
                violations.Add(token);
        }

        Assert.Empty(violations);
    }

    [Fact]
    public void runtime_public_declarations_extend_contract_types_only()
    {
        string bindingRoot = BindingRoot();
        var contractTypes = ReadPublicTypeDeclarations(
            Path.Combine(bindingRoot, "src", "Zlink", "Contracts"));
        var runtimeTypes = ReadPublicTypeDeclarationLocations(
            Path.Combine(bindingRoot, "src", "Zlink", "Runtime"));

        var violations = new List<string>();
        foreach ((string TypeName, string Path) runtimeType in runtimeTypes)
        {
            if (!contractTypes.Contains(runtimeType.TypeName))
                violations.Add($"{runtimeType.Path}:{runtimeType.TypeName}");
        }

        Assert.Empty(violations);
    }

    private static string ReadZlinkSource([CallerFilePath] string file = "")
    {
        string sourceRoot = Path.Combine(BindingRoot(file), "src", "Zlink");
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

    private static string ReadSampleAndPerfSource([CallerFilePath] string file = "")
    {
        string bindingRoot = BindingRoot(file);
        string[] roots =
        {
            Path.Combine(bindingRoot, "samples"),
            Path.Combine(bindingRoot, "perf")
        };
        var chunks = new List<string>();
        foreach (string root in roots)
        {
            foreach (string path in Directory.EnumerateFiles(root, "*.cs",
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
        }
        return string.Join('\n', chunks);
    }

    private static string BindingRoot([CallerFilePath] string file = "")
    {
        string repoRoot = Path.GetFullPath(Path.Combine(
            Path.GetDirectoryName(file)!,
            "..", "..", "..", ".."));
        return Path.Combine(repoRoot, "bindings", "dotnet");
    }

    private static HashSet<string> ReadPublicTypeDeclarations(string root)
    {
        var declarations = new HashSet<string>(StringComparer.Ordinal);
        foreach ((string TypeName, _) in ReadPublicTypeDeclarationLocations(root))
            declarations.Add(TypeName);
        return declarations;
    }

    private static List<(string TypeName, string Path)> ReadPublicTypeDeclarationLocations(
        string root)
    {
        var declarations = new List<(string TypeName, string Path)>();
        foreach (string path in Directory.EnumerateFiles(root, "*.cs",
                     SearchOption.AllDirectories))
        {
            string source = File.ReadAllText(path);
            foreach (Match match in PublicTypeDeclaration.Matches(source))
                declarations.Add((match.Groups[1].Value, path));
        }
        return declarations;
    }
}
