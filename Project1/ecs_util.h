#pragma once
#include <vector>
#include <string>
#include <utility>

namespace ecs
{
	using entityId = int;
	using typeId = size_t;

	struct typeIdList : std::vector<typeId>
	{
		typeIdList() = default;
		typeIdList(std::initializer_list<typeId> l)
			: std::vector<typeId>(l)
		{
		}
	};

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

	struct type_desc
	{
		std::string name;
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

	template<class ...Ts>
	const typeIdList& getTypes() {
#if 0
		typeIdList ret{ type_id<Ts>()... };
		makeVectorUniqueAndSorted(ret);
		return ret;
#else
		static bool needsSort = true;
		static typeIdList ret{ type_id<Ts>()... };
		if (needsSort)
		{
			makeVectorUniqueAndSorted(ret);
			needsSort = false;
		}
		return ret;
#endif
	}
}