#pragma once
#include <vector>

namespace ecs
{
	using entityId = int;
	using typeId = size_t;

	template<typename T>
	struct type_helper { 
		static int id() 
		{ 
			static int tmp = 0; 
			if (tmp == 0) 
				return 1; 
			return 0; 
		} 
	};

	template<typename T>
	typeId type_id() { 
		auto ptr = &type_helper<T>::id;
		return reinterpret_cast<typeId>(ptr);
	}

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

	template<class ...Ts>
	std::vector<typeId> getTypes() {
		std::vector<typeId> ret{ type_id<Ts>()... };
		makeVectorUniqueAndSorted(ret);
		return ret;
	}
}