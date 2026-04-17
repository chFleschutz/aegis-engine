module;

#include <filesystem>

export module Aegis.Core.Asset;

export namespace Aegis::Core
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

	template<typename T>
	concept IsLoadable = requires(const std::filesystem::path & path)
	{
		{ T::load(path) } -> std::same_as<std::shared_ptr<T>>;
	};
}
