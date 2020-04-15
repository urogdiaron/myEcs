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
}