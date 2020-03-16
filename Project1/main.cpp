#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <stdio.h>
#include <algorithm>
#include <tuple>

namespace self_registering
{
	template<class TBase>
	struct Factory
	{
		virtual std::unique_ptr<TBase> create() = 0;
	};

	template<class TSpecial, class TBase>
	struct Factory_Spec : public Factory<TBase>
	{
		std::unique_ptr<TBase> create() override 
		{ 
			return std::make_unique<TSpecial>(); 
		}
	};

	template<class TBase, class TKey>
	struct FactoryMap
	{
		static FactoryMap& getInstance()
		{
			static FactoryMap factoryMapSingleton;
			return factoryMapSingleton;
		}

		static std::unique_ptr<TBase> create(const TKey& typeName)
		{
			auto& factories = getInstance().factories_;
			auto it = factories.find(typeName);
			if (it != factories.end())
			{
				return it->second->create();
			}
			return nullptr;
		}

		std::unordered_map<TKey, std::unique_ptr<Factory<TBase>>> factories_;
	};

	template<class TSpecial, class TBase, class TKey>
	struct FactoryRegistrator
	{
		FactoryRegistrator()
		{
			auto& factoryMap = FactoryMap<TBase, TKey>::getInstance();
			factoryMap.factories_[TSpecial::getKey()] = std::make_unique<Factory_Spec<TSpecial, TBase>>();
		}
	};

	template<class TSpecial, class TBase, class TKey>
	struct SelfRegistering
	{
		static FactoryRegistrator<TSpecial, TBase, TKey> registrator_;
		static void init(){}
		virtual void forceInitialization() { registrator_; }
	};

	template<class TSpecial, class TBase, class TKey>
	FactoryRegistrator<TSpecial, TBase, TKey> SelfRegistering<TSpecial, TBase, TKey>::registrator_ = {};
}

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

	struct ComponentArrayBase
	{
		virtual ~ComponentArrayBase() {}
		virtual typeId getTypeId() const = 0;
		virtual void createEntity() = 0;
		virtual void deleteEntity(int elementIndex) = 0;
		virtual void copyFromArray(int sourceElementIndex, const ComponentArrayBase* sourceArray) = 0;
	};

	using ComponentFactory = self_registering::FactoryMap<ComponentArrayBase, typeId>;

	template<class T>
	using ComponentRegister = self_registering::SelfRegistering<T, ComponentArrayBase, typeId>;


	template<class T>
	struct ComponentArray : public ComponentArrayBase, public ComponentRegister<ComponentArray<T>>
	{
		static typeId getKey(){ return type_id<T>(); }

		typeId getTypeId() const override { return type_id<T>(); }

		void createEntity() override
		{
			components_.push_back(T{});
		}

		void deleteEntity(int elementIndex)
		{
			deleteFromVectorUnsorted(components_, elementIndex);
		}

		void copyFromArray(int sourceElementIndex, const ComponentArrayBase* sourceArray)
		{
			auto sourceArrayCasted = static_cast<const ComponentArray<T>*>(sourceArray);
			components_.push_back(sourceArrayCasted->components_[sourceElementIndex]);
		}

		std::vector<T> components_;
	};

	struct Archetype
	{
		Archetype() {}
		Archetype(const std::vector<typeId>& typeIds)
		{
			for (auto& t : typeIds)
			{
				addType(t);
			}
		}

		ComponentArrayBase* get(typeId tid)
		{
			auto it = componentArrays_.find(tid);
			if (it != componentArrays_.end())
			{
				return it->second.get();
			}
			return nullptr;
		}

		template<class T>
		bool add()
		{
			return addType(type_id<T>());
		}

		bool addType(typeId tid)
		{
			auto it = componentArrays_.find(tid);
			if (it != componentArrays_.end())
				return false;

			componentArrays_[tid] = ComponentFactory::create(tid);

			containedTypes_.push_back(tid);
			std::sort(containedTypes_.begin(), containedTypes_.end());

			return true;
		}

		int createEntity(entityId id)
		{
			int newIndex = entityIds_.size();
			entityIds_.push_back(id);
			for (auto& it : componentArrays_)
			{
				it.second->createEntity();
			}

			return newIndex;
		}

		void deleteEntity(int elementIndex)
		{
			deleteFromVectorUnsorted(entityIds_, elementIndex);
			for (auto& it : componentArrays_)
			{
				it.second->deleteEntity(elementIndex);
			}
		}

		int copyFromEntity(entityId id, int sourceElementIndex, const Archetype* sourceArchetype)
		{
			for (auto& itDestArray : componentArrays_)
			{
				auto itSourceArray = sourceArchetype->componentArrays_.find(itDestArray.second->getTypeId());
				if (itSourceArray == sourceArchetype->componentArrays_.end())
				{
					itDestArray.second->createEntity();
				}
				else
				{
					itDestArray.second->copyFromArray(sourceElementIndex, itSourceArray->second.get());
				}
			}

			int newIndex = entityIds_.size();
			entityIds_.push_back(id);
			return newIndex;
		}

		bool hasAllComponents(const std::vector<typeId>& typeIds) const
		{
			int requiredTypeIndex = 0;
			int containedTypeIndex = 0;
			int foundCount = 0;

			while (true)
			{
				if (requiredTypeIndex >= typeIds.size())
					break;

				if (containedTypeIndex >= containedTypes_.size())
					break;

				if (containedTypes_[containedTypeIndex] == typeIds[requiredTypeIndex])
				{
					requiredTypeIndex++;
					containedTypeIndex++;
					foundCount++;
				}
				else
				{
					containedTypeIndex++;
				}
			}

			return foundCount == typeIds.size();
		}

		std::vector<typeId> containedTypes_;
		std::vector<entityId> entityIds_;
		std::unordered_map<typeId, std::unique_ptr<ComponentArrayBase>> componentArrays_;
	};

	struct EntityCommand
	{
		enum class Type { CreateEntity, DeleteEntity, ChangeComponents };
		Type type_;
		entityId id_;
		std::vector<typeId> typeIds_;
	};

	struct Ecs
	{
		template<class T>
		void get_impl(const std::vector<typeId>& typeIds, std::vector<std::vector<T>*>& out)
		{
			for (auto& itArchetype : archetypes_)
			{
				if (!itArchetype->hasAllComponents(typeIds))
				{
					continue;
				}

				ComponentArrayBase* componentArray = itArchetype->get(type_id<T>());
				if (componentArray)
				{
					out.push_back(&static_cast<ComponentArray<T>*>(componentArray)->components_);
				}
			}
		}

		template<>
		void get_impl(const std::vector<typeId>& typeIds, std::vector<std::vector<entityId>*>& out)
		{
			for (auto& itArchetype : archetypes_)
			{
				if (!itArchetype->hasAllComponents(typeIds))
				{
					continue;
				}

				out.push_back(&itArchetype->entityIds_);
			}
		}

		template<class T, class ...Ts>
		void get_impl(const std::vector<typeId>& typeIds, std::vector<std::vector<T>*>& out, std::vector<std::vector<Ts>*>&... restOut)
		{
			get_impl(typeIds, out);
			get_impl(typeIds, restOut...);
		}

		template<class ...Ts>
		std::tuple<std::vector<std::vector<entityId>*>, std::vector<std::vector<Ts>*>...> get()
		{
			std::tuple<std::vector<std::vector<entityId>*>, std::vector<std::vector<Ts>*>...> ret = {};
			std::apply([&](auto& ...x) { get_impl(getTypes<Ts...>(), x...); }, ret);
			return ret;
		}

		std::tuple<int, Archetype*> createArchetype(const std::vector<typeId>& typeIds)
		{
			int i = 0;
			for (auto& itArchetype : archetypes_)
			{
				if (itArchetype->containedTypes_ == typeIds)
				{
					return { i, itArchetype.get() };
				}
				i++;
			}

			auto& insertedItem = archetypes_.emplace_back(std::make_unique<Archetype>(typeIds));
			return { i, insertedItem.get() };
		}

		entityId createEntity(const std::vector<typeId>& typeIds)
		{
			entityId newEntityId = nextEntityId++;
			auto [archIndex, archetype] = createArchetype(typeIds);
			int elementIndex = archetype->createEntity(newEntityId);
			entityDataIndexMap_[newEntityId] = { archIndex, elementIndex };
			return newEntityId;
		}

		bool deleteEntity(entityId id)
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return false;

			Archetype* arch = archetypes_[it->second.archetypeIndex].get();
			arch->deleteEntity(it->second.elementIndex);

			for (int i = it->second.elementIndex; i < arch->entityIds_.size(); i++)
			{
				entityId changedId = arch->entityIds_[i];
				auto changedIt = entityDataIndexMap_.find(changedId);
				if (changedIt != entityDataIndexMap_.end())
				{
					changedIt->second.elementIndex = i;
				}
			}

			return true;
		}

		template<class T>
		void registerType(const char* name)
		{
			ComponentArray<T>::getKey(); // Just to make sure that the selfRegistering code gets generated
			auto it = typeIdsByName_.find(name);
			if (it != typeIdsByName_.end())
				return;		// Multiple registration

			typeIdsByName_[name] = type_id<T>();
		}

		typeId getTypeIdByName(const char* name)
		{
			auto it = typeIdsByName_.find(name);
			if (it != typeIdsByName_.end())
				return it->second;

			return 0;
		}

		void changeComponents(entityId id, const std::vector<typeId>& typeIds)
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return;

			Archetype* oldArchetype = archetypes_[it->second.archetypeIndex].get();
			auto newTypes = typeIds;
			makeVectorUniqueAndSorted(newTypes);

			auto [archIndex, archetype] = createArchetype(newTypes);
			if (archetype == oldArchetype)
				return;

			int newElementIndex = archetype->copyFromEntity(id, it->second.elementIndex, oldArchetype);
			deleteEntity(id);
			entityDataIndexMap_[id] = { archIndex, newElementIndex };
		}

		template<class T>
		T* getComponent(entityId id) const
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return nullptr;

			auto itComponentArray = archetypes_[it->second.archetypeIndex]->componentArrays_.find(type_id<T>());
			if (itComponentArray == archetypes_[it->second.archetypeIndex]->componentArrays_.end())
			{
				return nullptr;
			}

			return &static_cast<ComponentArray<T>*>(itComponentArray->second.get())->components_[it->second.elementIndex];
		}

		void printArchetypes()
		{
			auto fnGetTypeNameById = [&](typeId id) -> const char*
			{
				for (auto& it : typeIdsByName_)
				{
					if (it.second == id)
						return it.first.c_str();
				}
				return "";
			};

			int i = 0;
			for (auto& arch : archetypes_)
			{
				printf("Archetype %0.2d\n", i);
				printf("Types: ");
				for (auto type : arch->containedTypes_)
				{
					printf("%s, ", fnGetTypeNameById(type));
				}
				printf("\n");

				printf("Entities: ");
				for (auto id : arch->entityIds_)
				{
					printf("%0.2d, ", id);
				}
				printf("\n\n");
				i++;
			}
		}

		void executeEntityCommandBuffer(std::vector<EntityCommand>& commandBuffer)
		{
			for(int i = 0; i < commandBuffer.size(); i++)
			{
				auto& cmd = commandBuffer[i];
				switch (cmd.type_)
				{
				case EntityCommand::Type::CreateEntity:
				{
					entityId newId = createEntity(cmd.typeIds_);
					if (cmd.id_ < 0)
					{
						for (int j = i + 1; j < commandBuffer.size(); j++)
						{
							auto& nextCmd = commandBuffer[j];
							if (nextCmd.id_ == cmd.id_)
							{
								nextCmd.id_ = newId;
							}
						}
					}
				}
				break;
				case EntityCommand::Type::DeleteEntity:
				{
					deleteEntity(cmd.id_);
				}
				break;
				case EntityCommand::Type::ChangeComponents:
				{
					changeComponents(cmd.id_, cmd.typeIds_);
				}
				break;
				}
			}
		}

		struct entityDataIndex
		{
			int archetypeIndex;
			int elementIndex;
		};

		std::unordered_map<std::string, typeId> typeIdsByName_;
		std::unordered_map<entityId, entityDataIndex> entityDataIndexMap_;
		std::vector<std::unique_ptr<Archetype>> archetypes_;
		entityId nextEntityId = 1;
	};

	template<class ...Ts>
	struct View
	{
		View(Ecs& ecs)
			: data_(ecs.get<Ts...>())
		{}

		struct iterator
		{
			iterator() = default;

			iterator(View* v) : view(v)
			{
				auto& entityIds = std::get<std::vector<std::vector<entityId>*>>(view->data_);
				if (entityIds.size() > 0)
				{
					vectorIndex = 0;
					if (entityIds[vectorIndex]->size() > 0)
						elementIndex = 0;
				}
			}

			iterator& operator++()
			{
				auto& entityIds = std::get<std::vector<std::vector<entityId>*>>(view->data_);
				if ((int)entityIds[vectorIndex]->size() - 1 > elementIndex)
				{
					elementIndex++;
				}
				else
				{
					if ((int)entityIds.size() - 1 > vectorIndex)
					{
						vectorIndex++;
						if (entityIds[vectorIndex]->size() > 0)
							elementIndex = 0;
						else
							elementIndex = -1;
					}
					else
					{
						vectorIndex = -1;
					}
				}
				return *this;
			}

			bool operator==(const iterator& rhs) const
			{
				if (!isValid() && !rhs.isValid())
					return true;

				return (vectorIndex == rhs.vectorIndex && elementIndex == rhs.elementIndex);
			}

			bool operator!=(const iterator& rhs) const
			{
				return !(*this == rhs);
			}

			bool isValid() const
			{
				return vectorIndex >= 0 && elementIndex >= 0;
			}

			std::tuple<entityId, Ts&...> operator*()
			{
				return getCurrentTuple();
			}

			template<class T>
			void getCurrentTuple_impl(T*& elementOut)
			{
				auto& vectors = std::get<std::vector<std::vector<T>*>>(view->data_);
				elementOut = &(*vectors[vectorIndex])[elementIndex];
			}

			template<class T, class ...Rest>
			void getCurrentTuple_impl(T*& elementOut, Rest*&... rest)
			{
				getCurrentTuple_impl(elementOut);
				getCurrentTuple_impl(rest...);
			}

			std::tuple<entityId, Ts&...> getCurrentTuple()
			{
				std::tuple<entityId*, Ts*...> ret;
				std::apply([&](auto& ...x) { getCurrentTuple_impl(x...); }, ret);
				return std::apply([&](auto& ...x) { return std::forward_as_tuple(*x...); }, ret);
			}

			View* view = nullptr;
			int vectorIndex = -1;
			int elementIndex = -1;
		};

		iterator begin() {
			return iterator(this);
		}

		iterator end()
		{
			return iterator();
		}

		entityId createEntity(const std::vector<typeId>& types)
		{
			entityId newId = -(entityCommandBuffer_.size() + 1);
			entityCommandBuffer_.push_back(EntityCommand{ EntityCommand::Type::CreateEntity, newId, types });
			return newId;
		}

		void deleteEntity(entityId id)
		{
			entityCommandBuffer_.push_back(EntityCommand{ EntityCommand::Type::DeleteEntity, id, {} });
		}

		void changeComponents(entityId id, const std::vector<typeId>& types)
		{
			entityCommandBuffer_.push_back(EntityCommand{ EntityCommand::Type::ChangeComponents, id, types });
		}

		std::vector<EntityCommand> entityCommandBuffer_;
		std::tuple<std::vector<std::vector<entityId>*>, std::vector<std::vector<Ts>*>...> data_;
	};
}

struct pos
{
	int x, y, z;
};

struct hp
{
	int health = 100;
};

struct sensor
{
	float radius = 5.0f;
};

void main()
{
	ecs::Ecs e;
	e.registerType<pos>("position");
	e.registerType<hp>("health");
	e.registerType<sensor>("sensor");

	std::vector<ecs::typeId> ph = { e.getTypeIdByName("position"), e.getTypeIdByName("health") };
	std::sort(ph.begin(), ph.end());
	
	std::vector<ecs::typeId> ps = { e.getTypeIdByName("position"), e.getTypeIdByName("sensor") };
	std::sort(ps.begin(), ps.end());

	for (int i = 0; i < 5; i++)
	{
		e.createEntity(ph);
		e.createEntity(ps);
	}

	for (auto& [id, pos] : ecs::View<pos>(e))
	{
		pos.x += id * 5;
	}

	for (auto& [id, pos, hp] : ecs::View<pos, hp>(e))
	{
		printf("id: %d; pos.x: %d; hp: %d\n", id, pos.x, hp.health);
	}

	e.deleteEntity(5);
	printf("entity #5 is deleted\n");

	e.deleteEntity(6);
	printf("entity #6 is deleted\n");

	e.deleteEntity(7);
	printf("entity #7 is deleted\n");

	e.printArchetypes();
	e.changeComponents(2, { e.getTypeIdByName("position"), e.getTypeIdByName("health") });
	printf("entity #2 sensor deleted and health added\n");
	e.printArchetypes();

	ecs::View<pos> positions(e);
	for (auto& [id, pos] : positions)
	{
		printf("id: %d; pos.x: %d\n", id, pos.x);
		if (pos.x > 15)
		{
			// csinalhatnank egy kis local ecs-t itt, ami mindent belerakunk. az execute pedig mergelne az eredetibe
			ecs::entityId newId = positions.createEntity(ecs::getTypes<pos, hp>());
			positions.changeComponents(newId, ecs::getTypes<pos, hp, sensor>());
			printf("new object created at id: %d; pos.x: %d\n", id, pos.x);
		}
	}

	for (auto& [id, pos, hp] : ecs::View<pos, hp>(e))
	{
		printf("id: %d; pos.x: %d; hp: %d\n", id, pos.x, hp.health);
	}
}
