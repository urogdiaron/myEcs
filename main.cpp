#include <vector>
#include <array>
#include <tuple>

//////////////////////////////////////////////////////////////////////////
/// TYPES

using typeId = size_t;

template<typename T>
struct type_helper { static void id() { } };

template<typename T>
typeId type_id() { return reinterpret_cast<typeId>(&type_helper<T>::id); }

template<class ...Ts>
std::vector<typeId> getTypes() {
	std::vector<typeId> ret{ type_id<Ts>()... };
	std::sort(ret.begin(), ret.end());
	return ret;
}

//////////////////////////////////////////////////////////////////////////

class IArchetype
{
public:
	virtual void* getComponent(typeId t) = 0;
};

template<class T>
void filterComponent(std::vector<T>& v, typeId t, void*& ret)
{
	if (type_id<T>() == t)
		ret = &v;
}

template<class ...Ts>
class Archetype : public IArchetype
{
public:
	Archetype(int capacity)
	{
		auto reserveVector = [capacity](auto&& x) { x.reserve(capacity); };
		std::apply([&](auto& ...x) {(reserveVector(x), ...); }, components);
	}

	void* getComponent(typeId t) override
	{
		void* ret = nullptr;
		std::apply([&](auto& ...v) {(filterComponent(v, t, ret), ...); }, components);
		return ret;
	}

	std::tuple<std::vector<Ts>...> components;
};

class Ecs
{
public:
	struct ArchetypeDesc
	{
		IArchetype* archetype;
		std::vector<typeId> containedTypes;
	};

	template<class ...Ts>
	void registerArchetype()
	{
		ArchetypeDesc desc;
		desc.archetype = new Archetype<Ts...>(256);
		desc.containedTypes = getTypes<Ts...>();
		archetypes.push_back(desc);
	}

	template<class T>
	std::vector<std::vector<T>*> get()
	{
		std::vector<std::vector<T>*> ret;
		typeId neededType = type_id<T>();
		for (auto& desc : archetypes)
		{
			if (std::find(desc.containedTypes.begin(), desc.containedTypes.end(), neededType) != desc.containedTypes.end())
			{
				auto v = static_cast<std::vector<T>*>(desc.archetype->getComponent(neededType));
				if (v) ret.push_back(v);
			}
		}
		return ret;
	}

	std::vector<ArchetypeDesc> archetypes;
};

int main()
{
	Ecs ecs;
	ecs.registerArchetype<int, float>();
	ecs.registerArchetype<int, double>();

	auto a = ecs.get<int>();

	return 0;
}