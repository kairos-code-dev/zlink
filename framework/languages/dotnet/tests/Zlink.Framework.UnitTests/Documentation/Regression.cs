namespace Zlink.Framework.UnitTests.Documentation;

public sealed class RegressionTests
{
    private static readonly string[] DotNetDraftDocuments =
    [
        "README.ko.md",
        "registry-backed-routing-defaults.ko.md",
        "spot-timer-policy.ko.md",
        "handler-interfaces.ko.md",
        "aspnet-core-channel-messaging.ko.md",
        "aspnet-core-spot.ko.md",
        "stage-wrapper-on-spot.ko.md",
        "aspnet-core-stream.ko.md",
        "aspnet-core-actor.ko.md",
        "session-actor-dispatch.ko.md",
        "session-attached-actor-route.ko.md",
        "channel-handler-exposure-and-spot-route-transport.ko.md",
        "spot-node.ko.md",
        "streaming-client.ko.md",
        "stream-open-items.ko.md",
        "aspnet-core-monitoring.ko.md",
        "aspnet-core-registry.ko.md",
        "behavior-matrix.ko.md",
        "di-capability-exposure-policy.ko.md",
        "regression-test-matrix.ko.md",
        "lifecycle-and-failure-semantics.ko.md",
        "implementation-scope-and-nongoals.ko.md",
        "backend-dependency-policy.ko.md",
        "channel-messaging-samples.ko.md",
        "spot-samples.ko.md",
        "stream-samples.ko.md",
        "tictactoe-game-sample.ko.md",
        "bingo-game-sample.ko.md",
    ];

    [Fact]
    public void DotNetDraftDocuments_AllExposeRegressionTestSection()
    {
        var directory = GetDotNetDocRoot();
        // Narrative guide docs (guide/*.ko.md, not guide/samples/) are onboarding
        // prose, not contract docs: they are exempt from the regression-section
        // requirement. Samples remain contract-bound and stay in the strict set.
        var guideRoot = Path.Combine(directory, "guide");
        var actualDocuments = Directory
            .EnumerateFiles(directory, "*.ko.md", SearchOption.AllDirectories)
            .Where(path => !string.Equals(
                Path.GetDirectoryName(path), guideRoot, StringComparison.Ordinal))
            .Select(Path.GetFileName)
            .OfType<string>()
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(DotNetDraftDocuments.Order(StringComparer.Ordinal), actualDocuments);

        foreach (var document in DotNetDraftDocuments)
        {
            var path = ResolveDoc(document);
            var text = File.ReadAllText(path);
            var references = ExtractRegressionTestReferences(text).ToArray();

            Assert.Contains("회귀 테스트", text, StringComparison.Ordinal);
            Assert.Contains("| 테스트 케이스 | 확인 기준 |", text, StringComparison.Ordinal);
            Assert.NotEmpty(references);
            Assert.DoesNotContain(
                references,
                static reference => reference.StartsWith("planned:", StringComparison.Ordinal));
            Assert.Empty(references
                .GroupBy(static reference => reference, StringComparer.Ordinal)
                .Where(static group => group.Count() > 1)
                .Select(static group => group.Key));
        }
    }

    private static readonly string[] GuideNarrativeDocuments =
    [
        "01-overview.ko.md",
        "02-getting-started.ko.md",
        "03-concepts.ko.md",
        "04-channel-messaging.ko.md",
        "05-spot.ko.md",
        "06-actor-session.ko.md",
        "07-stream.ko.md",
        "08-registry.ko.md",
        "09-monitoring.ko.md",
        "10-feature-map.ko.md",
    ];

    [Fact]
    public void DotNetGuideNarrative_DocumentsExist_AndAreWellFormed()
    {
        var guideRoot = Path.Combine(GetDotNetDocRoot(), "guide");

        var actual = Directory
            .EnumerateFiles(guideRoot, "*.ko.md", SearchOption.TopDirectoryOnly)
            .Select(Path.GetFileName)
            .OfType<string>()
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(GuideNarrativeDocuments.Order(StringComparer.Ordinal), actual);

        foreach (var document in GuideNarrativeDocuments)
        {
            var text = File.ReadAllText(Path.Combine(guideRoot, document));
            Assert.Contains("<!-- framework-adapter-nav:start -->", text, StringComparison.Ordinal);
            Assert.Matches(@"(?m)^# .+", text);
        }
    }

    [Fact]
    public void DotNetDocs_SpotRouteChannelAcceptance_RulesStayDocumented()
    {
        var docRoot = GetDotNetDocRoot();
        var guideRoot = Path.Combine(docRoot, "guide");
        var guideAndSampleDocs = Directory
            .EnumerateFiles(guideRoot, "*.ko.md", SearchOption.AllDirectories)
            .ToArray();

        foreach (var path in guideAndSampleDocs)
        {
            var text = File.ReadAllText(path);
            Assert.DoesNotContain("AddChannel(", text, StringComparison.Ordinal);
            Assert.DoesNotContain("AddRouteChannel(", text, StringComparison.Ordinal);
        }

        var spotSpec = File.ReadAllText(ResolveDoc("aspnet-core-spot.ko.md"));
        var channelSpec = File.ReadAllText(
            ResolveDoc("aspnet-core-channel-messaging.ko.md"));
        var spotSamples = File.ReadAllText(ResolveDoc("spot-samples.ko.md"));
        var combined = string.Join(
            Environment.NewLine,
            spotSpec,
            channelSpec,
            spotSamples);

        Assert.Contains("AcceptSpotRoutesFromChannel", combined,
            StringComparison.Ordinal);
        Assert.Contains("AddClientServerChannel", combined,
            StringComparison.Ordinal);
        Assert.Contains("AddRouteMeshChannel", combined,
            StringComparison.Ordinal);
        Assert.DoesNotContain("NonPublic", combined, StringComparison.Ordinal);
        Assert.DoesNotContain("internal/private", combined,
            StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void DotNetRegressionMatrix_References_AllDraftDocuments()
    {
        var matrix = File.ReadAllText(ResolveDoc("regression-test-matrix.ko.md"));

        foreach (var document in DotNetDraftDocuments)
        {
            Assert.Contains(document, matrix, StringComparison.Ordinal);
        }
    }

    [Fact]
    public void DotNetDraftRegressionTestReferences_Resolve_ToActiveTestMethods()
    {
        var activeTests = GetActiveTestMethods();

        foreach (var document in DotNetDraftDocuments)
        {
            var path = ResolveDoc(document);
            var text = File.ReadAllText(path);
            var references = ExtractRegressionTestReferences(text).ToArray();
            Assert.NotEmpty(references);

            foreach (var reference in references)
            {
                Assert.Contains(reference, activeTests);
            }
        }
    }

    [Fact]
    public void DotNetRegressionMatrix_Includes_ExecutionSerialization_Guards()
    {
        var matrix = File.ReadAllText(ResolveDoc("regression-test-matrix.ko.md"));

        Assert.Contains("Entry Spot actor mailbox dispatch", matrix, StringComparison.Ordinal);
        Assert.Contains("local actor mailbox dispatch", matrix, StringComparison.Ordinal);
        Assert.Contains("user Spot actor dispatch serialization", matrix, StringComparison.Ordinal);
        Assert.Contains("session actor dispatch ordering", matrix, StringComparison.Ordinal);
        Assert.Contains("actor dispatch location after mailbox wait", matrix, StringComparison.Ordinal);
        Assert.Contains("session callback task dispatch", matrix, StringComparison.Ordinal);
        Assert.Contains("session callback 직렬성", matrix, StringComparison.Ordinal);
        Assert.Contains("runtime task exception observation", matrix, StringComparison.Ordinal);
        Assert.Contains("execution queue cancellation semantics", matrix, StringComparison.Ordinal);
    }

    [Fact]
    public void DotNetSessionActorDispatch_Documents_ExecutionSerialization_Core_Code()
    {
        var document = File.ReadAllText(ResolveDoc("session-actor-dispatch.ko.md"));

        Assert.Contains("## 2.3 실행 직렬화 핵심 코드", document, StringComparison.Ordinal);
        Assert.Contains("internal sealed class ZLinkSerialWorkItem", document, StringComparison.Ordinal);
        Assert.Contains("internal sealed class ZLinkSerialExecutionQueue", document, StringComparison.Ordinal);
        Assert.Contains("private readonly SemaphoreSlim _drainGate = new(1, 1);", document, StringComparison.Ordinal);
        Assert.Contains("internal interface IZLinkRuntimeErrorSink", document, StringComparison.Ordinal);
        Assert.Contains("internal sealed class ZLinkRuntimeTaskRunner", document, StringComparison.Ordinal);
        Assert.Contains("TaskScheduler.Default", document, StringComparison.Ordinal);
        Assert.Contains("internal sealed class ZLinkStreamSessionRuntime", document, StringComparison.Ordinal);
        Assert.Contains("internal sealed class ZLinkActorDispatchRuntime", document, StringComparison.Ordinal);
        Assert.Contains("internal sealed class ZLinkUserSpotRuntime", document, StringComparison.Ordinal);
        Assert.Contains("internal sealed class ZLinkEntrySpotRuntime", document, StringComparison.Ordinal);
        Assert.Contains("internal sealed class ZLinkNodeMessageRuntime", document, StringComparison.Ordinal);
        var normalized = NormalizeWhitespace(document);
        Assert.Contains("queue 에 들어간 work item 을 중간에서", normalized, StringComparison.Ordinal);
        Assert.Contains("fire-and-forget handler 예외", normalized, StringComparison.Ordinal);
    }

    private static string NormalizeWhitespace(string value)
    {
        return string.Join(' ', value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries));
    }

    private static string GetDotNetDocRoot()
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);

        while (current is not null)
        {
            var candidate = Path.Combine(
                current.FullName,
                "framework",
                "languages",
                "dotnet",
                "doc");

            if (Directory.Exists(candidate))
            {
                return candidate;
            }

            current = current.Parent;
        }

        throw new DirectoryNotFoundException(
            "Could not find framework/languages/dotnet/doc from test runtime.");
    }

    private static string ResolveDoc(string fileName)
    {
        var matches = Directory.EnumerateFiles(
            GetDotNetDocRoot(),
            fileName,
            SearchOption.AllDirectories).ToArray();

        return matches.Length switch
        {
            1 => matches[0],
            0 => throw new FileNotFoundException(
                $"Could not find '{fileName}' under framework/languages/dotnet/doc."),
            _ => throw new InvalidOperationException(
                $"Ambiguous document '{fileName}': {string.Join(", ", matches)}"),
        };
    }

    private static IReadOnlyCollection<string> ExtractRegressionTestReferences(string text)
    {
        var sectionMatch = System.Text.RegularExpressions.Regex.Match(
            text,
            @"^## [^\r\n]*회귀 테스트[^\r\n]*\r?\n(?<body>.*?)(?=^## |\z)",
            System.Text.RegularExpressions.RegexOptions.Multiline | System.Text.RegularExpressions.RegexOptions.Singleline);

        Assert.True(sectionMatch.Success, "Missing regression test section.");

        return System.Text.RegularExpressions.Regex
            .Matches(
                sectionMatch.Groups["body"].Value,
                @"^\| `(?<test>[^`]+)` \|",
                System.Text.RegularExpressions.RegexOptions.Multiline)
            .Select(static match => match.Groups["test"].Value)
            .ToArray();
    }

    private static IReadOnlySet<string> GetActiveTestMethods()
    {
        var testsRoot = GetTestsRoot();
        var sourceFiles = new HashSet<string>(StringComparer.Ordinal);

        foreach (var projectPath in Directory.EnumerateFiles(testsRoot, "*.csproj", SearchOption.AllDirectories))
        {
            AddProjectSources(projectPath, sourceFiles);
        }

        var activeTests = new HashSet<string>(StringComparer.Ordinal);
        foreach (var sourceFile in sourceFiles)
        {
            var text = File.ReadAllText(sourceFile);
            var classMatches = System.Text.RegularExpressions.Regex.Matches(
                text,
                @"\bclass\s+(?<class>[A-Za-z_][A-Za-z0-9_]*)");

            if (classMatches.Count == 0)
            {
                continue;
            }

            foreach (System.Text.RegularExpressions.Match methodMatch in System.Text.RegularExpressions.Regex.Matches(
                         text,
                         @"\bpublic\s+(?:async\s+)?(?:Task|void)\s+(?<method>[A-Za-z_][A-Za-z0-9_]*)\s*\("))
            {
                var className = classMatches
                    .Cast<System.Text.RegularExpressions.Match>()
                    .Last(match => match.Index < methodMatch.Index)
                    .Groups["class"].Value;
                var methodName = methodMatch.Groups["method"].Value;
                if (HasFactOrTheoryAttribute(text, methodMatch.Index))
                {
                    activeTests.Add($"{className}.{methodName}");
                }
            }
        }

        return activeTests;
    }

    private static bool HasFactOrTheoryAttribute(string text, int methodIndex)
    {
        var lookbackStart = Math.Max(0, methodIndex - 300);
        var prefix = text[lookbackStart..methodIndex];
        return prefix.Contains("[Fact]", StringComparison.Ordinal)
            || prefix.Contains("[Theory]", StringComparison.Ordinal)
            || (prefix.Contains("[Fact(", StringComparison.Ordinal)
                && !prefix.Contains("Skip", StringComparison.Ordinal))
            || (prefix.Contains("[Theory(", StringComparison.Ordinal)
                && !prefix.Contains("Skip", StringComparison.Ordinal));
    }

    private static void AddProjectSources(string projectPath, ISet<string> sourceFiles)
    {
        var projectDirectory = Path.GetDirectoryName(projectPath)
            ?? throw new InvalidOperationException($"Could not get project directory for '{projectPath}'.");
        var projectSources = Directory
            .EnumerateFiles(projectDirectory, "*.cs", SearchOption.AllDirectories)
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}", StringComparison.Ordinal)
                && !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}", StringComparison.Ordinal))
            .Select(Path.GetFullPath)
            .ToHashSet(StringComparer.Ordinal);

        var document = System.Xml.Linq.XDocument.Load(projectPath);
        foreach (var remove in document.Descendants("Compile").SelectMany(static element => element.Attributes("Remove")))
        {
            foreach (var removedPath in ResolveProjectPattern(projectDirectory, remove.Value))
            {
                projectSources.Remove(removedPath);
            }
        }

        foreach (var include in document.Descendants("Compile").SelectMany(static element => element.Attributes("Include")))
        {
            foreach (var includedPath in ResolveProjectPattern(projectDirectory, include.Value))
            {
                projectSources.Add(includedPath);
            }
        }

        foreach (var sourceFile in projectSources)
        {
            sourceFiles.Add(sourceFile);
        }
    }

    private static IEnumerable<string> ResolveProjectPattern(string projectDirectory, string pattern)
    {
        if (pattern.Contains('*', StringComparison.Ordinal))
        {
            yield break;
        }

        var path = Path.GetFullPath(Path.Combine(projectDirectory, pattern));
        if (File.Exists(path))
        {
            yield return path;
        }
    }

    private static string GetTestsRoot()
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);

        while (current is not null)
        {
            var candidate = Path.Combine(current.FullName, "framework", "languages", "dotnet", "tests");
            if (Directory.Exists(candidate))
            {
                return candidate;
            }

            current = current.Parent;
        }

        throw new DirectoryNotFoundException(
            "Could not find framework/languages/dotnet/tests from test runtime.");
    }
}
