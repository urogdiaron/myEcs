#pragma once
#include "archetype.h"

namespace ecs
{
	struct Ecs
	{
	public:
		Ecs() = default;
		Ecs(const Ecs & src) = delete;

	private:
		template<typename...>
		friend struct View;
		
		template<typename...>
		friend struct EntityCommand_Create;

		template<class T>
		void get_impl(const typeQueryList& typeIds, std::vector<Archetype*>& archetypesOut, std::vector<std::vector<T>*>& out)
		{
			out.clear();
			for (auto& itArchetype : archetypes_)
			{
				if (!itArchetype->hasAllComponents(typeIds))
				{
					continue;
				}

				if (itArchetype->entityIds_.size() == 0)
					continue;

				ComponentArrayBase* componentArray = itArchetype->get(getTypeId<T>());
				if (componentArray)
				{
					out.push_back(&static_cast<ComponentArray<T>*>(componentArray)->components_);
				}
			}
		}

		template<>
		void get_impl(const typeQueryList& typeIds, std::vector<Archetype*>& archetypesOut, std::vector<std::vector<entityId>*>& out)
		{
			out.clear();
			archetypesOut.clear();
			for (auto& itArchetype : archetypes_)
			{
				if (!itArchetype->hasAllComponents(typeIds))
				{
					continue;
				}

				if (itArchetype->entityIds_.size() == 0)
					continue;

				archetypesOut.push_back(itArchetype.get());
				out.push_back(&itArchetype->entityIds_);
			}
		}

		template<class T, class ...Ts>
		void get_impl(const typeQueryList& typeIds, std::vector<Archetype*>& archetypesOut, std::vector<std::vector<T>*>& out, std::vector<std::vector<Ts>*>&... restOut)
		{
			get_impl(typeIds, archetypesOut, out);
			get_impl(typeIds, archetypesOut, restOut...);
		}

		template<class ...Ts>
		const std::tuple<std::vector<std::vector<entityId>*>, std::vector<std::vector<std::decay_t<Ts>>*>...>& get(const typeQueryList& typeIds, std::vector<Archetype*>*& archetypesOut)
		{
			static std::tuple<std::vector<std::vector<entityId>*>, std::vector<std::vector<std::decay_t<Ts>>*>...> ret = {};
			static std::vector<Archetype*> archetypesStatic;
			std::apply([&](auto& ...x) { get_impl(typeIds, archetypesStatic, x...); }, ret);
			archetypesOut = &archetypesStatic;
			return ret;
		}

		template<class T>
		int buildTypeQueryList_impl(typeQueryList& inOut, TypeQueryItem::Mode mode) const
		{
			TypeQueryItem newItem;
			newItem.type = getTypeId<T>();
			newItem.mode = mode;
			if (mode == TypeQueryItem::Write)
			{
				if constexpr (std::is_const<T>::value)
				{
					newItem.mode = TypeQueryItem::Read;
				}
			}
			inOut.push_back(newItem);
			return 0;
		}

		template<class ...Ts>
		void buildTypeQueryList(typeQueryList& inOut, TypeQueryItem::Mode mode) const
		{
			auto tmp = { buildTypeQueryList_impl<Ts>(inOut, mode)..., 0 };
			std::sort(inOut.begin(), inOut.end(), [](const TypeQueryItem& a, const TypeQueryItem& b) -> int { return a.type < b.type; });
		}

		std::tuple<int, Archetype*> createArchetype(const typeIdList& typeIds)
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

			auto& insertedItem = archetypes_.emplace_back(std::make_unique<Archetype>(typeIds, componentArrayFactory_));
			return { i, insertedItem.get() };
		}

		entityId createEntity_impl(const typeIdList& typeIds)
		{
			entityId newEntityId = nextEntityId++;
			auto [archIndex, archetype] = createArchetype(typeIds);
			int elementIndex = archetype->createEntity(newEntityId);
			entityDataIndexMap_[newEntityId] = { archIndex, elementIndex };
			return newEntityId;
		}
		template<class T>
		typeId getTypeId_impl() const
		{
			static size_t typeIndex = typeDescriptors_.size();
			if (typeDescriptors_.size() > typeIndex)
				return typeDescriptors_[typeIndex].get();
			return nullptr;
		}

		template<class T>
		typeId getTypeId() const
		{
			return getTypeId_impl<std::decay_t<T>>();
		}

		template<class... Ts>
		const typeIdList& getTypeIds() const
		{
			static bool needsSort = true;
			static typeIdList ret{ getTypeId<Ts>()... };
			if (needsSort)
			{
				makeVectorUniqueAndSorted(ret);
				needsSort = false;
			}
			return ret;
		}
	public:

		template<class T>
		void registerType(const char* name)
		{
			typeId oldTypeId = getTypeId<T>();
			if (oldTypeId)
			{
				printf("Component \"%s\" is already registered!", name);
				return;
			}

			auto& typeDesc = typeDescriptors_.emplace_back(std::make_unique<TypeDescriptor>());
			typeDesc->index = (int)typeDescriptors_.size() - 1;
			typeDesc->name = name;
			componentArrayFactory_.addFactoryFunction<T>(typeDesc.get());
		}

		template<class T>
		void setComponent(entityId id, const T& value)
		{
			T* comp = getComponent<T>(id);
			if (comp)
				*comp = value;
		}

		template<class ...Ts>
		entityId createEntity(const Prefab<Ts...>& prefab)
		{
			return std::apply([&](auto& ...x) { return createEntity<Ts...>(x...); }, prefab.defaultValues);
		}

		template<class ...Ts, class ...Us>
		entityId createEntity(const Prefab<Ts...>& prefab, const Us&... initialValue)
		{
			auto prefabCopy = prefab;
			prefabCopy.setDefaultValue(initialValue...);
			return createEntity(prefabCopy);
		}

		template<class ...Ts>
		entityId createEntity()
		{
			entityId ret = createEntity_impl(getTypeIds<Ts...>());
			return ret;
		}

		template<class ...Ts>
		entityId createEntity(const Ts&... initialValue)
		{
			entityId ret = createEntity_impl(getTypeIds<Ts...>());
			int tmp[] = {(setComponent(ret, initialValue), 0)...};
			return ret;
		}

		bool deleteEntity(entityId id)
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return false;

			Archetype* arch = archetypes_[it->second.archetypeIndex].get();
			arch->deleteEntity(it->second.elementIndex);

			for (int i = it->second.elementIndex; i < (int)arch->entityIds_.size(); i++)
			{
				entityId changedId = arch->entityIds_[i];
				auto changedIt = entityDataIndexMap_.find(changedId);
				if (changedIt != entityDataIndexMap_.end())
				{
					changedIt->second.elementIndex = i;
				}
			}

			entityDataIndexMap_.erase(it);

			return true;
		}

		template<class T>
		void addComponent(entityId id, const T& data)
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return;

			Archetype* oldArchetype = archetypes_[it->second.archetypeIndex].get();
			typeIdList newTypes = oldArchetype->containedTypes_;
			newTypes.push_back(getTypeId<T>());
			changeComponents(id, newTypes);
			setComponent(id, data);
		}

		void deleteComponents(entityId id, const typeIdList& typeIds)
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return;

			Archetype* oldArchetype = archetypes_[it->second.archetypeIndex].get();
			typeIdList remainingTypes;
			remainingTypes.reserve(oldArchetype->containedTypes_.size());
			for (auto t : oldArchetype->containedTypes_)
			{
				auto it = std::find(typeIds.begin(), typeIds.end(), t);
				if (it == typeIds.end())
				{
					remainingTypes.push_back(t);
				}
			}
			
			changeComponents(id, remainingTypes);
		}

		void changeComponents(entityId id, const typeIdList& typeIds)
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
		bool hasComponent(entityId id) const
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return false;

			auto itComponentArray = archetypes_[it->second.archetypeIndex]->componentArrays_.find(getTypeId<T>());
			if (itComponentArray == archetypes_[it->second.archetypeIndex]->componentArrays_.end())
			{
				return false;
			}

			return true;
		}

		template<class T>
		T* getComponent(entityId id) const
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return nullptr;

			auto itComponentArray = archetypes_[it->second.archetypeIndex]->componentArrays_.find(getTypeId<T>());
			if (itComponentArray == archetypes_[it->second.archetypeIndex]->componentArrays_.end())
			{
				return nullptr;
			}

			return &static_cast<ComponentArray<T>*>(itComponentArray->second.get())->components_[it->second.elementIndex];
		}

private:
		template<class T>
		void getComponents_impl(entityId id, Archetype* archetype, int elementIndex, T*& out)
		{
			auto itComponentArray = archetype->componentArrays_.find(getTypeId<T>());
			if (itComponentArray == archetype->componentArrays_.end())
			{
				out = nullptr;
				return;
			}

			out = &static_cast<ComponentArray<T>*>(itComponentArray->second.get())->components_[elementIndex];
		}

		template<class T, class... Ts>
		void getComponents_impl(entityId id, Archetype* archetype, int elementIndex, T*& out, Ts*&... restOut)
		{
			getComponents_impl(id, archetype, elementIndex, out);
			getComponents_impl(id, archetype, elementIndex, restOut...);
		}


public:

		template<class... Ts>
		std::tuple<Ts*...> getComponents(entityId id)
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return std::tuple<Ts *...>{};

			Archetype* archetype = archetypes_[it->second.archetypeIndex].get();

			std::tuple<Ts*...> ret = {};
			std::apply([&](auto& ...x) { getComponents_impl(id, archetype, it->second.elementIndex, x...); }, ret);
			return ret;
		}

		void printArchetypes()
		{
			int i = 0;
			for (auto& arch : archetypes_)
			{
				printf("Archetype %0.2d\n", i);
				printf("Types: ");
				for (auto type : arch->containedTypes_)
				{
					printf("%s, ", type->name.c_str());
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

		struct entityDataIndex
		{
			int archetypeIndex;
			int elementIndex;
		};

		ComponentArrayFactory componentArrayFactory_;
		std::vector<std::unique_ptr<TypeDescriptor>> typeDescriptors_;	// we store pointers so the raw TypeDescriptor* will stay stable for sure
		std::unordered_map<entityId, entityDataIndex> entityDataIndexMap_;
		std::vector<std::unique_ptr<Archetype>> archetypes_;
		std::vector<std::unique_ptr<struct EntityCommand>> entityCommandBuffer_;
		std::unordered_map<entityId, entityId> temporaryEntityIdRemapping_;		// for EntityCommand_Create
		entityId nextEntityId = 1;
	};
}
