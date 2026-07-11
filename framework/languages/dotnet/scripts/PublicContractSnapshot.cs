using System.Globalization;
using System.Reflection;
using System.Text;

internal static class PublicContractSnapshot
{
    public static string Render(IEnumerable<Assembly> assemblies)
    {
        var lines = new List<string>();
        var nullability = new NullabilityInfoContext();
        foreach (var assembly in assemblies
                     .Distinct()
                     .OrderBy(static item => item.GetName().Name, StringComparer.Ordinal))
        {
            lines.Add($"assembly {assembly.GetName().Name}");
            foreach (var type in assembly.GetExportedTypes()
                         .OrderBy(static item => item.FullName, StringComparer.Ordinal))
                RenderType(type, nullability, lines);
        }

        return string.Join('\n', lines) + "\n";
    }

    private static void RenderType(
        Type type,
        NullabilityInfoContext nullability,
        ICollection<string> lines)
    {
        var kind = type.IsInterface
            ? "interface"
            : type.IsEnum
                ? "enum"
                : type.IsValueType
                    ? "struct"
                    : type.IsAbstract && type.IsSealed
                        ? "static-class"
                        : type.IsAbstract
                            ? "abstract-class"
                            : type.IsSealed
                                ? "sealed-class"
                                : "class";
        var bases = new List<string>();
        if (type.BaseType is { } baseType && baseType != typeof(object) && baseType != typeof(ValueType))
            bases.Add(FormatType(baseType));
        bases.AddRange(type.GetInterfaces().Select(static item => FormatType(item)).Order(StringComparer.Ordinal));
        lines.Add($"  type {kind} {FormatType(type)}{FormatBases(bases)}");
        RenderGenericConstraints(type.GetGenericArguments(), "    ", lines);

        foreach (var field in type.GetFields(BindingFlags.Public | BindingFlags.Static | BindingFlags.Instance |
                                             BindingFlags.DeclaredOnly)
                     .Where(static field => !field.IsSpecialName)
                     .OrderBy(FieldKey, StringComparer.Ordinal))
        {
            var modifiers = field.IsLiteral
                ? "const "
                : field.IsStatic
                    ? field.IsInitOnly ? "static readonly " : "static "
                    : field.IsInitOnly ? "readonly " : string.Empty;
            var value = field.IsLiteral ? $" = {FormatValue(field.GetRawConstantValue())}" : string.Empty;
            lines.Add($"    field {modifiers}{FormatType(field.FieldType)} {field.Name}{value}");
        }

        foreach (var constructor in type.GetConstructors(BindingFlags.Public | BindingFlags.Instance |
                                                          BindingFlags.DeclaredOnly)
                     .OrderBy(MethodKey, StringComparer.Ordinal))
            lines.Add($"    ctor {type.Name.Split('`')[0]}({FormatParameters(constructor, nullability)})");

        foreach (var property in type.GetProperties(BindingFlags.Public | BindingFlags.Static |
                                                    BindingFlags.Instance | BindingFlags.DeclaredOnly)
                     .OrderBy(PropertyKey, StringComparer.Ordinal))
        {
            var accessor = property.GetMethod ?? property.SetMethod;
            var staticText = accessor?.IsStatic == true ? "static " : string.Empty;
            var propertyNullability = TryCreate(nullability, property);
            var accessors = $"{{ {(property.GetMethod?.IsPublic == true ? "get; " : string.Empty)}" +
                            $"{(property.SetMethod?.IsPublic == true ? "set; " : string.Empty)}}}";
            var index = property.GetIndexParameters().Length == 0
                ? property.Name
                : $"this[{FormatParameters(property.GetIndexParameters(), nullability)}]";
            lines.Add($"    property {staticText}{FormatType(property.PropertyType, propertyNullability)} {index} {accessors}");
        }

        foreach (var @event in type.GetEvents(BindingFlags.Public | BindingFlags.Static | BindingFlags.Instance |
                                              BindingFlags.DeclaredOnly)
                     .OrderBy(static item => item.Name, StringComparer.Ordinal))
        {
            var staticText = @event.AddMethod?.IsStatic == true ? "static " : string.Empty;
            lines.Add($"    event {staticText}{FormatType(@event.EventHandlerType!)} {@event.Name}");
        }

        foreach (var method in type.GetMethods(BindingFlags.Public | BindingFlags.Static | BindingFlags.Instance |
                                               BindingFlags.DeclaredOnly)
                     .Where(static method => !IsAccessor(method))
                     .OrderBy(MethodKey, StringComparer.Ordinal))
        {
            var modifiers = method.IsStatic ? "static " : string.Empty;
            if (method.IsAbstract) modifiers += "abstract ";
            else if (method.IsVirtual && method.GetBaseDefinition() == method) modifiers += "virtual ";
            var generic = method.IsGenericMethodDefinition
                ? $"<{string.Join(", ", method.GetGenericArguments().Select(static argument => argument.Name))}>"
                : string.Empty;
            var returnNullability = TryCreate(nullability, method.ReturnParameter);
            lines.Add(
                $"    method {modifiers}{FormatType(method.ReturnType, returnNullability)} {method.Name}{generic}({FormatParameters(method, nullability)})");
            RenderGenericConstraints(method.GetGenericArguments(), "      ", lines);
        }
    }

    private static string FormatParameters(MethodBase method, NullabilityInfoContext nullability) =>
        FormatParameters(method.GetParameters(), nullability);

    private static string FormatParameters(
        IReadOnlyList<ParameterInfo> parameters,
        NullabilityInfoContext nullability) =>
        string.Join(", ", parameters.Select(parameter =>
        {
            var modifier = parameter.GetCustomAttribute<ParamArrayAttribute>() is not null
                ? "params "
                : parameter.IsOut
                    ? "out "
                    : parameter.ParameterType.IsByRef
                        ? parameter.IsIn ? "in " : "ref "
                        : string.Empty;
            var defaultValue = parameter.HasDefaultValue
                ? $" = {FormatValue(parameter.DefaultValue)}"
                : string.Empty;
            return $"{modifier}{FormatType(parameter.ParameterType, TryCreate(nullability, parameter))} {parameter.Name}{defaultValue}";
        }));

    private static void RenderGenericConstraints(
        IEnumerable<Type> arguments,
        string indent,
        ICollection<string> lines)
    {
        foreach (var argument in arguments.Where(static item => item.IsGenericParameter))
        {
            var constraints = new List<string>();
            var attributes = argument.GenericParameterAttributes;
            if ((attributes & GenericParameterAttributes.ReferenceTypeConstraint) != 0) constraints.Add("class");
            if ((attributes & GenericParameterAttributes.NotNullableValueTypeConstraint) != 0) constraints.Add("struct");
            if ((attributes & GenericParameterAttributes.DefaultConstructorConstraint) != 0
                && !constraints.Contains("struct", StringComparer.Ordinal))
                constraints.Add("new()");
            constraints.AddRange(argument.GetGenericParameterConstraints().Select(static item => FormatType(item)));
            if (constraints.Count > 0)
                lines.Add($"{indent}where {argument.Name} : {string.Join(", ", constraints)}");
        }
    }

    private static string FormatType(Type type, NullabilityInfo? nullability = null)
    {
        if (type.IsByRef) return FormatType(type.GetElementType()!, nullability);
        if (type.IsArray)
            return $"{FormatType(type.GetElementType()!, nullability?.ElementType)}[{new string(',', type.GetArrayRank() - 1)}]{NullableSuffix(type, nullability)}";
        if (type.IsPointer) return $"{FormatType(type.GetElementType()!)}*";
        if (type.IsGenericParameter) return type.Name + NullableSuffix(type, nullability);
        if (!type.IsGenericType) return (type.FullName ?? type.Name).Replace('+', '.') + NullableSuffix(type, nullability);

        var definition = type.GetGenericTypeDefinition();
        var name = (definition.FullName ?? definition.Name).Replace('+', '.');
        name = name[..name.IndexOf('`')];
        var arguments = type.GetGenericArguments();
        var nullableArguments = nullability?.GenericTypeArguments ?? [];
        var rendered = arguments.Select((argument, index) =>
            FormatType(argument, index < nullableArguments.Length ? nullableArguments[index] : null));
        return $"{name}<{string.Join(", ", rendered)}>{NullableSuffix(type, nullability)}";
    }

    private static string NullableSuffix(Type type, NullabilityInfo? nullability) =>
        !type.IsValueType && nullability?.ReadState == NullabilityState.Nullable ? "?" : string.Empty;

    private static NullabilityInfo? TryCreate(NullabilityInfoContext context, PropertyInfo property)
    {
        try { return context.Create(property); }
        catch { return null; }
    }

    private static NullabilityInfo? TryCreate(NullabilityInfoContext context, ParameterInfo parameter)
    {
        try { return context.Create(parameter); }
        catch { return null; }
    }

    private static string FormatValue(object? value) => value switch
    {
        null => "null",
        string text => $"\"{text.Replace("\\", "\\\\").Replace("\"", "\\\"")}\"",
        char character => $"'{character}'",
        bool boolean => boolean ? "true" : "false",
        Enum enumValue => $"{FormatType(enumValue.GetType())}.{enumValue}",
        IFormattable formattable => formattable.ToString(null, CultureInfo.InvariantCulture) ?? string.Empty,
        _ => value.ToString() ?? string.Empty
    };

    private static string FormatBases(IReadOnlyCollection<string> bases) =>
        bases.Count == 0 ? string.Empty : $" : {string.Join(", ", bases)}";

    private static bool IsAccessor(MethodInfo method) =>
        method.IsSpecialName && (method.Name.StartsWith("get_", StringComparison.Ordinal)
                                 || method.Name.StartsWith("set_", StringComparison.Ordinal)
                                 || method.Name.StartsWith("add_", StringComparison.Ordinal)
                                 || method.Name.StartsWith("remove_", StringComparison.Ordinal));

    private static string FieldKey(FieldInfo field) => $"{field.Name}:{FormatType(field.FieldType)}";
    private static string PropertyKey(PropertyInfo property) =>
        $"{property.Name}:{FormatType(property.PropertyType)}:{string.Join(',', property.GetIndexParameters().Select(static item => FormatType(item.ParameterType)))}";
    private static string MethodKey(MethodBase method) =>
        $"{method.Name}`{(method.IsGenericMethod ? method.GetGenericArguments().Length : 0)}({string.Join(',', method.GetParameters().Select(static item => FormatType(item.ParameterType)))})";
}
