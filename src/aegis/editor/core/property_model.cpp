module aegis.editor.core;
import std;
import aegis.reflect;

namespace aegis::editor
{
namespace
{
using reflect::FieldKind;
using reflect::FieldWidget;

// The drag step of a field with none in its metadata, in the widget's unit.
constexpr double DEFAULT_INTEGER_STEP = 1.0;
constexpr double DEFAULT_FLOAT_STEP = 0.01;
constexpr double DEFAULT_ANGLE_STEP = 0.5; // Degrees

auto radiansToDegrees(std::optional<double> radians) -> std::optional<double>
{
    if (!radians)
        return std::nullopt;
    return *radians * 180.0 / std::numbers::pi;
}

auto vectorSize(FieldKind kind) -> int
{
    switch (kind)
    {
    case FieldKind::Vec2: return 2;
    case FieldKind::Vec3: return 3;
    case FieldKind::Vec4: return 4;
    case FieldKind::Quat: return 3; // Euler angles
    default: return 1;
    }
}

auto widgetOf(const reflect::FieldInfo& field) -> PropertyWidget
{
    // The builder accepts a widget hint only on kinds that take it (TypeBuilder::field)
    switch (field.meta.widget)
    {
    case FieldWidget::Color: return PropertyWidget::Color;
    case FieldWidget::Angle: return PropertyWidget::Angle;
    case FieldWidget::Slider: return PropertyWidget::Slider;
    case FieldWidget::Default: break;
    }

    switch (field.kind)
    {
    case FieldKind::Bool: return PropertyWidget::Checkbox;
    case FieldKind::String: return PropertyWidget::Text;
    case FieldKind::Quat: return PropertyWidget::Rotation;
    case FieldKind::Enum: return PropertyWidget::Combo;
    case FieldKind::AssetRef: return PropertyWidget::AssetSlot;
    case FieldKind::Int:
    case FieldKind::UInt:
    case FieldKind::Float:
    case FieldKind::Double:
    case FieldKind::Vec2:
    case FieldKind::Vec3:
    case FieldKind::Vec4: return PropertyWidget::Drag;
    }
    return PropertyWidget::Drag;
}

auto defaultStep(const reflect::FieldInfo& field, PropertyWidget widget) -> double
{
    if (widget == PropertyWidget::Angle || widget == PropertyWidget::Rotation)
        return DEFAULT_ANGLE_STEP;
    return field.kind == FieldKind::Int || field.kind == FieldKind::UInt ? DEFAULT_INTEGER_STEP : DEFAULT_FLOAT_STEP;
}

auto isWordStart(std::string_view name, std::size_t i) -> bool
{
    const auto c = static_cast<unsigned char>(name[i]);
    const auto previous = static_cast<unsigned char>(name[i - 1]);
    if (std::isupper(c))
        return !std::isupper(previous) || (i + 1 < name.size() && std::islower(static_cast<unsigned char>(name[i + 1])));
    return std::isdigit(c) && !std::isdigit(previous);
}
}

auto fieldLabel(std::string_view name) -> std::string
{
    std::string label;
    label.reserve(name.size() + 4);
    bool wordStart = true;
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        const auto c = static_cast<unsigned char>(name[i]);
        if (c == '_')
        {
            wordStart = true;
            continue;
        }
        if (!wordStart && i > 0 && isWordStart(name, i))
            wordStart = true;
        if (wordStart && !label.empty())
            label.push_back(' ');
        label.push_back(static_cast<char>(wordStart ? std::toupper(c) : c));
        wordStart = false;
    }
    return label;
}

auto propertyRow(const reflect::FieldInfo& field) -> PropertyRow
{
    PropertyRow row{
        .field = &field,
        .label = fieldLabel(field.name),
        .widget = widgetOf(field),
        .components = vectorSize(field.kind),
        .min = field.meta.min,
        .max = field.meta.max,
        .readOnly = field.meta.readOnly,
    };
    if (row.widget == PropertyWidget::Angle)
    {
        row.min = radiansToDegrees(row.min);
        row.max = radiansToDegrees(row.max);
        row.step = field.meta.step ? *radiansToDegrees(field.meta.step) : DEFAULT_ANGLE_STEP;
    }
    else
    {
        row.step = field.meta.step.value_or(defaultStep(field, row.widget));
    }
    return row;
}

auto propertyRows(const reflect::TypeInfo& type) -> std::vector<PropertyRow>
{
    std::vector<PropertyRow> rows;
    rows.reserve(type.fields.size());
    for (const auto& field : type.fields)
        rows.push_back(propertyRow(field));
    return rows;
}
}
