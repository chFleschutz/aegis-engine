export module aegis.scene.component_traits;

import std;

export namespace aegis
{
template<typename T>
struct ComponentTraits
{
    static constexpr bool is_required = false;
};

template<typename T>
concept IsRequiredComponent = ComponentTraits<T>::is_required;

template<typename T>
concept IsOptionalComponent = !IsRequiredComponent<T>;

template<typename T>
concept TagComponent = std::is_empty_v<T>;
}
