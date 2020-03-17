#pragma once
#include <vector>

namespace ecs
{
	using entityId = int;
	using typeId = int;

	template<typename T>
	struct type_helper { static void id() { } };

	template<typename T>
	typeId type_id() { return reinterpret_cast<typeId>(&type_helper<T>::id); }

	template<class T>
	void deleteFromVectorUnsorted(std::vector<T>& v, int index)
	{
		if (v.size() - 1 > index)
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

	template<class ...Ts>
	std::vector<typeId> getTypes() {
		std::vector<typeId> ret{ type_id<Ts>()... };
		makeVectorUniqueAndSorted(ret);
		return ret;
	}
}