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
	void get_impl(const std::vector<typeId>& typeIds, std::vector<std::vector<T>*>& out)
	{
		for (auto& desc : archetypes)
		{
			// if all the typeIds are present in the desc, we're good
			size_t requiredTypeIndex = 0;
			size_t containedTypeCount = desc.containedTypes.size();
			for (size_t i = 0; i < containedTypeCount; i++)
			{
				if (desc.containedTypes[i] == typeIds[requiredTypeIndex])
					requiredTypeIndex++;
			}

			if(requiredTypeIndex < typeIds.size())
				continue;

			auto v = static_cast<std::vector<T>*>(desc.archetype->getComponent(type_id<T>()));
			if (v) 
				out.push_back(v);
		}
	}

	template<class T, class ...Ts>
	void get_impl(const std::vector<typeId>& typeIds, std::vector<std::vector<T>*>& out, std::vector<std::vector<Ts>*>&... restOut)
	{
		get_impl(typeIds, out);
		get_impl(typeIds, restOut...);
	}

	template<class ...Ts> 
	std::tuple<std::vector<std::vector<Ts>*>...> get()
	{
		std::tuple<std::vector<std::vector<Ts>*>...> ret = {};
		std::apply([&](auto& ...x) { get_impl(getTypes<Ts...>(), x...); }, ret);
		return ret;
	}


	std::vector<ArchetypeDesc> archetypes;
};

int main()
{
	Ecs ecs;
	ecs.registerArchetype<int, float>();
	ecs.registerArchetype<double, float, int>();
	ecs.registerArchetype<int, double>();

	auto a = ecs.get<int, float>();

	return 0;
}