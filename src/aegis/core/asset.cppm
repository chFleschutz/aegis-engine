export module aegis.core.asset;

import std;

export namespace aegis::core
{
class Asset
{
public:
    virtual ~Asset() = default;

    [[nodiscard]] auto path() const -> const std::filesystem::path& { return m_path; }
    void setPath(std::filesystem::path path) { m_path = path; }

protected:
    std::filesystem::path m_path;
};

template<typename T>
concept IsAsset = std::derived_from<T, Asset>;
}
