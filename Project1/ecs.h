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
		void get_impl(const typeQueryList& typeIds, const std::vector<Archetype*>& matchingArchetypes, std::vector<std::vector<T>*>& out)
		{
			out.clear();
			for (auto& itArchetype : matchingArchetypes)
			{
				ComponentArrayBase* componentArray = itArchetype->get(getTypeId<T>());
				if (componentArray)
				{
					out.push_back(&static_cast<ComponentArray<T>*>(componentArray)->components_);
				}
			}
		}

		template<>
		void get_impl(const typeQueryList& typeIds, const std::vector<Archetype*>& matchingArchetypes, std::vector<std::vector<entityId>*>& out)
		{
			out.clear();
			for (auto& itArchetype : matchingArchetypes)
			{
				out.push_back(&itArchetype->entityIds_);
			}
		}

		template<class T, class ...Ts>
		void get_impl(const typeQueryList& typeIds, const std::vector<Archetype*>& matchingArchetypes, std::vector<std::vector<T>*>& out, std::vector<std::vector<Ts>*>&... restOut)
		{
			get_impl(typeIds, matchingArchetypes, out);
			get_impl(typeIds, matchingArchetypes, restOut...);
		}

		template<class ...Ts>
		const std::tuple<std::vector<std::vector<entityId>*>, std::vector<std::vector<std::decay_t<Ts>>*>...>& get(const typeQueryList& typeIds, std::vector<Archetype*>*& archetypesOut)
		{
			static std::tuple<std::vector<std::vector<entityId>*>, std::vector<std::vector<std::decay_t<Ts>>*>...> ret = {};
			static std::vector<Archetype*> archetypesStatic;
			archetypesStatic.clear();
			for (auto& archetype : archetypes_)
			{
				if (archetype->hasAllComponents(typeIds) && archetype->entityIds_.size() > 0)
					archetypesStatic.push_back(archetype.get());
			}

			std::apply([&](auto& ...x) { get_impl(typeIds, archetypesStatic, x...); }, ret);
			archetypesOut = &archetypesStatic;
			return ret;
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
			static typeIdList ret = typeIdList((int)typeDescriptors_.size(), { getTypeId<Ts>()... });
			return ret;
		}
	public:

		template<class T>
		void registerType(const char* name, ComponentType componentType = ComponentType::Regular)
		{
			typeId oldTypeId = getTypeId<T>();
			if (oldTypeId)
			{
				printf("Component \"%s\" is already registered!\n", name);
				return;
			}

			auto& typeDesc = typeDescriptors_.emplace_back(std::make_unique<TypeDescriptor>());
			typeDesc->index = (int)typeDescriptors_.size() - 1;
			typeDesc->type = componentType;
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

		// If you call this from the outside, keepStateComponents needs to be true. False is only for internal usage.
		bool deleteEntity(entityId id, bool keepStateComponents = true)
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return false;

			Archetype* arch = archetypes_[it->second.archetypeIndex].get();

			if (keepStateComponents)
			{
				// Check if the archetype had State type components
				// If it did, don't actually delete the entity. Keep the state components.
				for (auto typeDesc : arch->containedTypes_.getTypeIds())
				{
					if (typeDesc->type == ComponentType::State)
					{
						typeIdList stateComponentsOnly = arch->containedTypes_.createTypeListStateComponentsOnly();
						changeComponents(id, stateComponentsOnly);
						return true;
					}
				}
			}

			// Delete the entity and clean up the map indices
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
			newTypes.addTypes({ getTypeId<T>() });
			changeComponents(id, newTypes);
			setComponent(id, data);
		}

		void deleteComponents(entityId id, const typeIdList& typeIds)
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return;

			Archetype* oldArchetype = archetypes_[it->second.archetypeIndex].get();
			typeIdList remainingTypes = oldArchetype->containedTypes_;
			remainingTypes.deleteTypes(typeIds.getTypeIds());
			changeComponents(id, remainingTypes);
		}

		void changeComponents(entityId id, const typeIdList& typeIds)
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return;

			Archetype* oldArchetype = archetypes_[it->second.archetypeIndex].get();
			auto newTypes = typeIds;

			if (newTypes.getTypeIds().size() == 0)
			{	// we deleted all the components
				deleteEntity(id, false);
				return;
			}

			auto [archIndex, archetype] = createArchetype(newTypes);
			if (archetype == oldArchetype)
				return;

			int newElementIndex = archetype->copyFromEntity(id, it->second.elementIndex, oldArchetype);
			deleteEntity(id, false);
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

		typeId getTypeIdByName(const std::string& typeName)
		{
			auto it = std::find_if(typeDescriptors_.begin(), typeDescriptors_.end(), [&](const std::unique_ptr<TypeDescriptor>& td) -> bool
				{
					return td->name == typeName;
				});

			if (it != typeDescriptors_.end())
				return it->get();

			return nullptr;
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
				for (auto type : arch->containedTypes_.getTypeIds())
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

		void save(std::ostream& stream) const
		{
			size_t count = typeDescriptors_.size();
			stream.write((char*)&count, sizeof(size_t));
			for (auto& t : typeDescriptors_)
			{
				stream.write((const char*)&t->index, sizeof(t->index));
				count = t->name.size();
				stream.write((char*)&count, sizeof(size_t));
				stream.write(t->name.data(), count);
			}

			// Collect all archetypes that are equivalent even after disregarding state components
			auto entityMapCopy = std::unordered_map<entityId, entityDataIndex>();

			int archetypeIndex = 0;
			std::vector<uint8_t> skipArchetype(archetypes_.size());
			for (size_t iArch = 0; iArch < archetypes_.size(); iArch++)
			{
				if (skipArchetype[iArch])
					continue;
				auto archetype = archetypes_[iArch].get();
				typeIdList typeIdsToSave = archetype->containedTypes_.createTypeListWithOnlySavedComponents();
				if (typeIdsToSave.getTypeIds().size() == 0)
					continue;	// don't save empty archetypes

				std::vector<const Archetype*> archetypesToSave = { archetype };
				for (size_t iArchToCheck = iArch + 1; iArchToCheck < archetypes_.size(); iArchToCheck++)
				{
					if (skipArchetype[iArch])
						continue;
					auto archetypeToMerge = archetypes_[iArchToCheck].get();
					typeIdList typeIdsToCheck = archetypeToMerge->containedTypes_.createTypeListWithOnlySavedComponents();
					if (typeIdsToSave != typeIdsToCheck)
						continue;

					// These two archetypes are the same without state components
					skipArchetype[iArchToCheck] = 1;
					archetypesToSave.push_back(archetypeToMerge);
				}

				size_t entityCount = 0;
				for (auto arch : archetypesToSave)
				{
					for (auto& entity : arch->entityIds_)
					{
						auto& entityMapEntry = entityMapCopy[entity];
						entityMapEntry.archetypeIndex = archetypeIndex;
						entityMapEntry.elementIndex = (int)entityCount;
						entityCount++;
					}
				}

				if (entityCount == 0)
					continue;

				archetypeIndex++;

				typeIdsToSave.save(stream);
				stream.write((const char*)&entityCount, sizeof(entityCount));

				for (auto arch : archetypesToSave)
				{
					stream.write((const char*)arch->entityIds_.data(), arch->entityIds_.size() * sizeof(entityId));
				}

				auto typesSortedByIndex = typeIdsToSave.getTypeIds();
				std::sort(typesSortedByIndex.begin(), typesSortedByIndex.end(), [](const typeId& a, const typeId& b) -> int
					{
						return a->index < b->index;
					});

				for (auto t : typesSortedByIndex)
				{
					for (auto arch : archetypesToSave)
					{
						auto it = arch->componentArrays_.find(t);
						it->second->save(stream);
					}
				}
			}

			// write out an empty typeidlist to signal end of archetypes
			count = 0;
			stream.write((char*)&count, sizeof(size_t));

			count = entityMapCopy.size();
			stream.write((char*)&count, sizeof(size_t));
			for (auto& it : entityMapCopy)
			{
				stream.write((const char*)&it.first, sizeof(it.first));
				stream.write((char*)&it.second, sizeof(it.second));
			}

			stream.write((char*)&nextEntityId, sizeof(nextEntityId));
		}

		void load(std::istream& stream)
		{
			entityDataIndexMap_.clear();
			archetypes_.clear();
			entityCommandBuffer_.clear();
			nextEntityId = 1;

			size_t typeDescCount = 0;
			stream.read((char*)&typeDescCount, sizeof(typeDescCount));
			std::vector<typeId> typeIdsByLoadedIndex(typeDescCount);
			for (size_t i = 0; i < typeDescCount; i++)
			{
				TypeDescriptor td;
				stream.read((char*)&td.index, sizeof(td.index));
				size_t count;
				stream.read((char*)&count, sizeof(count));
				td.name.resize(count);
				stream.read(td.name.data(), count);
				
				typeId id = getTypeIdByName(td.name);
				typeIdsByLoadedIndex[td.index] = id;
			}

			while (true)
			{
				typeIdList loadedTypeIds = getTypeIds<>();
				loadedTypeIds.load(stream, typeIdsByLoadedIndex);

				if (loadedTypeIds.getTypeIds().size() == 0)
					break;

				auto& itArch = archetypes_.emplace_back(std::make_unique<Archetype>());
				itArch->load(stream, loadedTypeIds, typeIdsByLoadedIndex, componentArrayFactory_);
			}

			size_t entityCount = 0;
			stream.read((char*)&entityCount, sizeof(entityCount));
			for (size_t i = 0; i < entityCount; i++)
			{
				entityId id;
				stream.read((char*)&id, sizeof(id));
				entityDataIndex dataIndex;
				stream.read((char*)&dataIndex, sizeof(dataIndex));
				entityDataIndexMap_[id] = dataIndex;
			}

			stream.read((char*)&nextEntityId, sizeof(nextEntityId));
		}

		ComponentArrayFactory componentArrayFactory_;
		std::vector<std::unique_ptr<TypeDescriptor>> typeDescriptors_;	// we store pointers so the raw TypeDescriptor* will stay stable for sure
		std::unordered_map<entityId, entityDataIndex> entityDataIndexMap_;
		std::vector<std::unique_ptr<Archetype>> archetypes_;
		std::vector<std::unique_ptr<struct EntityCommand>> entityCommandBuffer_;
		std::unordered_map<entityId, entityId> temporaryEntityIdRemapping_;		// for EntityCommand_Create
		entityId nextEntityId = 1;
	};
}
