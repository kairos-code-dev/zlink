using System.Reflection;
using System.Text.Json;
using Microsoft.Extensions.Configuration;

namespace Zlink.Framework.E2E.Configuration;

internal static class E2eConfiguration
{
    private static readonly IReadOnlyDictionary<string, string> CollectionOptions =
        new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            ["ProviderEndpoint"] = "ProviderEndpoints",
            ["RoutePeer"] = "RoutePeers"
        };

    public static string Write(string directory, string name, object options)
    {
        Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, $"{name}.json");
        File.WriteAllText(path, JsonSerializer.Serialize(new { Options = options }));
        if (!OperatingSystem.IsWindows())
            File.SetUnixFileMode(path, UnixFileMode.UserRead | UnixFileMode.UserWrite);
        return path;
    }

    public static string WriteArguments(
        string directory,
        string name,
        IReadOnlyList<string> arguments)
    {
        if (arguments.Count % 2 != 0)
            throw new ArgumentException("Every configuration option requires a value.", nameof(arguments));

        var values = new Dictionary<string, object?>(StringComparer.OrdinalIgnoreCase);
        for (var index = 0; index < arguments.Count; index += 2)
        {
            var option = arguments[index];
            if (!option.StartsWith("--", StringComparison.Ordinal))
                throw new ArgumentException($"Invalid configuration option: {option}", nameof(arguments));

            var property = string.Concat(option[2..].Split('-')
                .Select(static part => char.ToUpperInvariant(part[0]) + part[1..]));
            var value = arguments[index + 1];
            var plural = CollectionOptions.TryGetValue(property, out var collectionProperty)
                ? collectionProperty
                : property.EndsWith('s') ? property : property + "s";
            if (CollectionOptions.ContainsKey(property) && !values.ContainsKey(plural))
            {
                values[plural] = new List<string> { value };
                continue;
            }
            if (values.TryGetValue(plural, out var repeated) && repeated is List<string> repeatedList)
            {
                repeatedList.Add(value);
            }
            else if (values.TryGetValue(property, out var current))
            {
                values.Remove(property);
                values[plural] = new List<string> { current?.ToString() ?? string.Empty, value };
            }
            else
            {
                values[property] = value;
            }
        }

        return Write(directory, name, values);
    }

    public static T Load<T>(string[] args)
        where T : class
    {
        if (args.Length != 2
            || !string.Equals(args[0], "--config", StringComparison.Ordinal)
            || string.IsNullOrWhiteSpace(args[1]))
            throw new ArgumentException("Usage: --config PATH");

        var path = Path.GetFullPath(args[1]);
        if (!File.Exists(path))
            throw new FileNotFoundException($"Configuration file was not found: {path}", path);

        var fileConfiguration = new ConfigurationBuilder()
            .AddJsonFile(path, optional: false, reloadOnChange: false)
            .Build();
        var defaults = MissingValueDefaults<T>(fileConfiguration);
        var configuration = new ConfigurationBuilder()
            .AddInMemoryCollection(defaults)
            .AddConfiguration(fileConfiguration)
            .Build();
        var options = configuration.GetRequiredSection("Options").Get<T>()
                      ?? throw new InvalidOperationException(
                          $"Options could not be bound to {typeof(T).Name}.");
        Validate(options);
        return options;
    }

    private static Dictionary<string, string?> MissingValueDefaults<T>(IConfiguration configuration)
        where T : class
    {
        var defaults = new Dictionary<string, string?>(StringComparer.OrdinalIgnoreCase);
        var constructor = typeof(T).GetConstructors().SingleOrDefault();
        if (constructor is null) return defaults;

        var nullability = new NullabilityInfoContext();
        foreach (var parameter in constructor.GetParameters())
        {
            var key = $"Options:{parameter.Name}";
            if (configuration.GetSection(key).Exists() || parameter.HasDefaultValue) continue;

            if (parameter.ParameterType.IsValueType)
            {
                defaults[key] = parameter.ParameterType == typeof(bool) ? "false" : "0";
            }
            else if (parameter.ParameterType == typeof(string)
                     && nullability.Create(parameter).ReadState == NullabilityState.Nullable)
            {
                defaults[key] = " ";
            }
        }

        return defaults;
    }

    private static void Validate<T>(T options)
        where T : class
    {
        var nullability = new NullabilityInfoContext();
        foreach (var property in typeof(T).GetProperties(BindingFlags.Instance | BindingFlags.Public))
        {
            var value = property.GetValue(options);
            if (value is string text)
            {
                if (string.IsNullOrWhiteSpace(text))
                {
                    if (nullability.Create(property).ReadState == NullabilityState.NotNull)
                        throw new InvalidOperationException($"Options.{property.Name} must not be empty.");
                    continue;
                }
                ValidateEndpoint(property.Name, text);
                continue;
            }

            if (value is null
                && property.PropertyType.IsClass
                && nullability.Create(property).ReadState == NullabilityState.NotNull)
                throw new InvalidOperationException($"Options.{property.Name} is required.");

            if (value is int number
                && (property.Name.EndsWith("TimeoutMs", StringComparison.Ordinal)
                    || property.Name.EndsWith("IntervalMs", StringComparison.Ordinal)
                    || property.Name.EndsWith("TtlMs", StringComparison.Ordinal))
                && number <= 0)
                throw new InvalidOperationException($"Options.{property.Name} must be greater than zero.");
        }
    }

    private static void ValidateEndpoint(string name, string value)
    {
        if (!name.EndsWith("Endpoint", StringComparison.Ordinal)
            && !name.EndsWith("Url", StringComparison.Ordinal)
            && !name.EndsWith("BaseUrl", StringComparison.Ordinal))
            return;

        var separator = value.LastIndexOf(':');
        var isHostPort = separator > 0
                         && separator + 1 < value.Length
                         && int.TryParse(value[(separator + 1)..], out var port)
                         && port is > 0 and <= 65535;
        if (!Uri.TryCreate(value, UriKind.Absolute, out _) && !isHostPort)
            throw new InvalidOperationException($"Options.{name} is not an absolute endpoint: {value}");
    }
}
