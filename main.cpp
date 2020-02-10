#include <vector>
#include <array>
#include <tuple>

template<typename T>
struct type { static void id() { } };

template<typename T>
size_t type_id() { return reinterpret_cast<size_t>(&type<T>::id); }

template<class ...Ts>
struct Archetype
{
	Archetype(int capacity)
	{
		auto resizeTheVector = [capacity](auto&& x) { x.resize(capacity); };
		std::apply([&](auto& ...x) {(..., resizeTheVector(x)); }, vectors);
	}

	auto getTypeIds() {
		return  { type_id<Ts>()... };
	}

	template<class T>
	std::vector<T>& getComponentData() { return std::get<std::vector<T>>(vectors); }

	std::tuple<std::vector<Ts>...> vectors;
};


int main()
{
	Archetype<int, float> v(5);
	return 0;
}