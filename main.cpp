#include <vector>
#include <array>
#include <tuple>

//////////////////////////////////////////////////////////////////////////
/// TYPES

using typeId = size_t;
using entity = size_t;

template<typename T>
struct type_helper { static void id() { } };

template<typename T>
typeId type_id() { return reinterpret_cast<typeId>(&type_helper<T>::id); }

template<class ...Ts>
std::vector<typeId> getTypes() {
	std::vector<typeId> ret{ type_id<Ts>()... };
	std::sort(ret.begin(), ret.end());
	ret.erase(std::unique(ret.begin(), ret.end()), ret.end());
	return ret;
}

//////////////////////////////////////////////////////////////////////////

template<class T>
void filterComponent(std::vector<T>& v, typeId t, void*& ret)
{
	if (type_id<T>() == t)
		ret = &v;
}

class IArchetype
{
public:
	virtual void* getComponent(typeId t) = 0;
	virtual void add(entity id) = 0;
};

template<class ...Ts>
class Archetype : public IArchetype
{
public:
	Archetype(int capacity)
	{
		entities.reserve(capacity);
		auto reserveVector = [capacity](auto&& x) { x.reserve(capacity); };
		std::apply([&](auto& ...x) {(reserveVector(x), ...); }, components);
	}

	void* getComponent(typeId t) override
	{
		void* ret = nullptr;
		std::apply([&](auto& ...v) {(filterComponent(v, t, ret), ...); }, components);
		return ret;
	}

	void add(entity id) override
	{
		entities.push_back(id);
		auto addToVector = [](auto&& x) { x.emplace_back(); };
		std::apply([&](auto& ...x) {(addToVector(x), ...); }, components);
	}

	void set(entity id, Ts... values)
	{
		auto it = std::find(entities.begin(), entities.end(), id);
		if (it == entities.end())
			return;

		size_t index = it - entities.begin();
		//...
	}

	std::vector<entity> entities;
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
	IArchetype* getArchetype()
	{
		auto typeIds = getTypes<Ts...>();

		for (auto& desc : archetypes)
		{
			if(desc.containedTypes == typeIds)
				return desc.archetype;
		}

		return nullptr;
	}

	template<class ...Ts>
	IArchetype* registerArchetype()
	{
		IArchetype* ret = getArchetype<Ts...>();
		if (!ret)
		{
			ArchetypeDesc desc;
			desc.archetype = new Archetype<Ts...>(256);
			desc.containedTypes = getTypes<Ts...>();
			archetypes.push_back(desc);
			ret = desc.archetype;
		}
		return ret;
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

	template<class ...Ts>
	entity add()
	{
		IArchetype* arch = registerArchetype<Ts...>();
		entity newId = nextEntityId++;
		arch->add(newId);
		return newId;
	}

	template<class ...Ts>
	entity add(Ts... defaultValues)
	{
		IArchetype* arch = registerArchetype<Ts...>();
		entity newId = nextEntityId++;
		arch->add(newId);

		static_cast<Archetype<Ts...>*>(arch)->set

		return newId;
	}

	std::vector<ArchetypeDesc> archetypes;
	entity nextEntityId = 1;
};

//////////////////////////////////////////////////////////////////////////
/// TESTS

struct position { float x = 0; float y = 0; };
struct velocity { float x = 0; float y = 0; };
struct health { float hp = 100; };

int main()
{
	Ecs ecs;

	ecs.add<position>();
	ecs.add<position>();
	ecs.add<position>();

	ecs.add<position, velocity>();
	ecs.add<velocity, position>();
	ecs.add<velocity, position, velocity>();

	ecs.add<position, health>();
	ecs.add<position, velocity, health>();

	auto [posVectors, velVectors] = ecs.get<position, velocity>();
	for (size_t iVec = 0; iVec < posVectors.size(); iVec++)
	{
		for (size_t i = 0; i < posVectors[iVec]->size(); i++)
		{
			auto& pos = (*posVectors[iVec])[i];
			auto& vel = (*velVectors[iVec])[i];

			pos.x += vel.x;
			pos.y += vel.y;
		}
	}

	return 0;
}