#pragma once
#include <vector>
#include <string>
#include <utility>

namespace ecs
{
	using entityId = int;

	using typeIndex = int;
	struct TypeDescriptor
	{
		typeIndex index;
		std::string name;
	};

	using typeId = TypeDescriptor*;

	struct typeIdList : std::vector<typeId>
	{
		typeIdList() = default;
		typeIdList(std::initializer_list<typeId> l)
			: std::vector<typeId>(l)
		{
		}
	};

	struct TypeQueryItem
	{
		enum Mode { Read, Write, Exclude, Required };
		typeId type;
		Mode mode;
	};

	using typeQueryList = std::vector<TypeQueryItem>;

	template<class T>
	void deleteFromVectorUnsorted(std::vector<T>& v, int index)
	{
		if ((int)v.size() - 1 > index)
		{
			std::swap(v[index], *(--v.end()));
		}
		v.pop_back();
	}

	template<class T>
	void makeVectorUniqueAndSorted(std::vector<T>& v)
	{
		std::sort(v.begin(), v.end());
		v.erase(std::unique(v.begin(), v.end()), v.end());
	}

	template<class... Ts>
	struct Prefab
	{
		Prefab() = default;

		template<class ...Us>
		Prefab(const Us&... defaultValues)
		{
			setDefaultValue(defaultValues...);
		}

		template<class T>
		void setDefaultValue(const T defaultValue)
		{
			static const bool typeIsInThePrefab = { (std::is_same_v<T, Ts> || ...) };
			static_assert(typeIsInThePrefab, "Prefab was initialized with default value of a type that is not in the prefab.");
			std::get<T>(defaultValues) = defaultValue;
		}

		template<class T, class ...Us>
		void setDefaultValue(T&& defaultValue, const Us&... defaultValues)
		{
			setDefaultValue(defaultValue);
			setDefaultValue(defaultValues...);
		}

		std::tuple<Ts...> defaultValues;
	};
}