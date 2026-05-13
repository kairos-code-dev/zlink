namespace Zlink.Framework.Tests;

public sealed class DocumentationRegressionTests
{
    private static readonly string[] DotNetDraftDocuments =
    [
        "README.ko.md",
        "handler-interfaces.ko.md",
        "aspnet-core-channel-messaging.ko.md",
        "aspnet-core-spot.ko.md",
        "stage-wrapper-on-spot.ko.md",
        "aspnet-core-stream.ko.md",
        "aspnet-core-actor.ko.md",
        "session-actor-dispatch.ko.md",
        "streaming-client.ko.md",
        "unity-stream-connector.ko.md",
        "stream-open-items.ko.md",
        "aspnet-core-monitoring.ko.md",
        "aspnet-core-registry.ko.md",
        "behavior-matrix.ko.md",
        "regression-test-matrix.ko.md",
        "lifecycle-and-failure-semantics.ko.md",
        "implementation-scope-and-nongoals.ko.md",
        "backend-dependency-policy.ko.md",
        "channel-messaging-samples.ko.md",
        "spot-samples.ko.md",
        "stream-samples.ko.md",
        "tictactoe-game-sample.ko.md",
    ];

    [Fact]
    public void DotNetDraftDocuments_AllExposeRegressionTestSection()
    {
        var directory = GetDotNetDraftDirectory();
        var actualDocuments = Directory
            .EnumerateFiles(directory, "*.ko.md")
            .Select(Path.GetFileName)
            .OfType<string>()
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(DotNetDraftDocuments.Order(StringComparer.Ordinal), actualDocuments);

        foreach (var document in DotNetDraftDocuments)
        {
            var path = Path.Combine(directory, document);
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

    [Fact]
    public void DotNetRegressionMatrix_References_AllDraftDocuments()
    {
        var directory = GetDotNetDraftDirectory();
        var matrix = File.ReadAllText(Path.Combine(directory, "regression-test-matrix.ko.md"));

        foreach (var document in DotNetDraftDocuments)
        {
            Assert.Contains(document, matrix, StringComparison.Ordinal);
        }
    }

    [Fact]
    public void DotNetDraftRegressionTestReferences_Resolve_ToActiveTestMethods()
    {
        var directory = GetDotNetDraftDirectory();
        var activeTests = GetActiveTestMethods();

        foreach (var document in DotNetDraftDocuments)
        {
            var path = Path.Combine(directory, document);
            var text = File.ReadAllText(path);
            var references = ExtractRegressionTestReferences(text).ToArray();
            Assert.NotEmpty(references);

            foreach (var reference in references)
            {
                Assert.Contains(reference, activeTests);
            }
        }
    }

    private static string GetDotNetDraftDirectory()
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);

        while (current is not null)
        {
            var candidate = Path.Combine(
                current.FullName,
                "framework",
                "doc",
                "spec",
                "draft",
                "framework-adapter",
                "bindings",
                "dotnet");

            if (Directory.Exists(candidate))
            {
                return candidate;
            }

            current = current.Parent;
        }

        throw new DirectoryNotFoundException(
            "Could not find framework/doc/spec/draft/framework-adapter/bindings/dotnet from test runtime.");
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
