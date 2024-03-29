#pragma once
#include "ecs.h"
#include "entitycommand.h"

namespace ecs
{
	Ecs::Ecs()
	{
		registerType<DontSaveEntity>("DontSaveEntity", ComponentType::Internal);
		registerType<DeletedEntity>("DeletedEntity", ComponentType::Internal);
	}
	
	std::tuple<int, Archetype*> Ecs::createArchetype(const typeIdList& typeIds)
	{
		int i = 0;
		int firstEmptyIndex = -1;
		for (auto& itArchetype : archetypes_)
		{
			if (itArchetype)
			{
				if (itArchetype->containedTypes_ == typeIds)
				{
					return { i, itArchetype.get() };
				}
			}
			else if(firstEmptyIndex < 0)
			{
				firstEmptyIndex = i;
			}
			i++;
		}

		if (firstEmptyIndex >= 0)
		{
			archetypes_[firstEmptyIndex] = std::make_unique<Archetype>(typeIds, firstEmptyIndex, this);
			return { firstEmptyIndex, archetypes_[firstEmptyIndex].get() };
		}
		else
		{
			auto& insertedItem = archetypes_.emplace_back(std::make_unique<Archetype>(typeIds, i, this));
			return { i, insertedItem.get() };
		}

	}
	
	entityId Ecs::createEntity_impl(const typeIdList& typeIds)
	{
		entityId newEntityId = nextEntityId++;
		auto [archIndex, archetype] = createArchetype(typeIds);
		entityDataIndex newIndex = archetype->createEntity(newEntityId);
		setEntityIndexMap(newEntityId, newIndex);
		return newEntityId;
	}

	template<class T>
	void addSharedComponentDataToList(tempList<ComponentData>& outList, const T& componentValue, typeId tid)
	{
		if (tid->type == ComponentType::Shared)
		{
			outList.push_back(ComponentData{ tid, (void*)&componentValue });
		}
	}

	template<class... Ts>
	tempList<ComponentData> Ecs::createSharedComponentDataList(const Ts&... componentValues)
	{
		tempList<ComponentData> ret;
		auto tmp = {(addSharedComponentDataToList(ret, componentValues, getTypeId<Ts>()), 0)...};
		return ret;
	}

	template<class ...Ts>
	entityId Ecs::createEntity(const Ts&... initialValue)
	{
		//EASY_FUNCTION();
		entityId newEntityId = nextEntityId++;
		typeIdList typeIds = getTypeIds<Ts...>();
		auto [archIndex, archetype] = createArchetype(typeIds);
		entityDataIndex newIndex = archetype->allocateEntity(createSharedComponentDataList(initialValue...));
		Chunk* chunk = archetype->chunks[newIndex.chunkIndex].get();
		chunk->getEntityIds()[newIndex.elementIndex] = newEntityId;
		chunk->setInitialComponentValues(newIndex.elementIndex, initialValue...);
		setEntityIndexMap(newEntityId, newIndex);
		return newEntityId;
	}

	// If you call this from the outside, keepStateComponents needs to be true. False is only for internal usage.
	bool Ecs::deleteEntity(entityId id, bool keepStateComponents)
	{
		auto it = entityDataIndexMap_.find(id);
		if (it == entityDataIndexMap_.end())
			return false;

		entityDataIndex entityIndex = it->second;
		if (entityIndex.archetypeIndex < 0)
			return false;

		Archetype* arch = archetypes_[entityIndex.archetypeIndex].get();

		if (keepStateComponents)
		{
			// Check if the archetype had State type components
			// If it did, don't actually delete the entity. Keep the state components.
			for (auto typeDesc : arch->containedTypes_.calcTypeIds(typeIds_))
			{
				if (typeDesc->type == ComponentType::State)
				{
					typeIdList stateComponentsOnly = arch->containedTypes_.createTypeListStateComponentsOnly(typeIds_);
					stateComponentsOnly.addTypes({ getTypeId<DeletedEntity>() });
					changeComponents(id, stateComponentsOnly);
					return true;
				}
			}
		}

		// Delete the entity and clean up the map indices
		entityId movedEntity = arch->deleteEntity(entityIndex);

		//entityDataIndex deletedIndex = entityIndex;
		//deletedIndex.archetypeIndex = -(entityIndex.archetypeIndex + 1);
		//if (keepStateComponents)
		//	deletedIndex.chunkIndex = -(entityIndex.chunkIndex + 1);
		//entityDataIndexMap_[id] = deletedIndex;

		entityDataIndexMap_.erase(id);

		if (arch->chunks.size() == 0)
			deleteArchetype(entityIndex.archetypeIndex);

		if (movedEntity)
			setEntityIndexMap(movedEntity, entityIndex);
		return true;
	}
	
	void Ecs::deleteComponents(entityId id, const typeIdList& typeIds)
	{
		auto it = entityDataIndexMap_.find(id);
		if (it == entityDataIndexMap_.end())
			return;

		Archetype* oldArchetype = archetypes_[it->second.archetypeIndex].get();
		typeIdList remainingTypes = oldArchetype->containedTypes_;
		remainingTypes.deleteTypes(typeIds);
		changeComponents(id, remainingTypes);
	}
	
	void Ecs::changeComponents(entityId id, const typeIdList& typeIds)
	{
		auto it = entityDataIndexMap_.find(id);
		if (it == entityDataIndexMap_.end())
			return;

		Archetype* oldArchetype = archetypes_[it->second.archetypeIndex].get();
		auto newTypes = typeIds;

		size_t typeCount = newTypes.calcTypeCount();
		if (typeCount == 0 ||
			(typeCount == 1 && newTypes.hasType(getTypeId<DeletedEntity>())))
		{	// we deleted all the components or only the deleted entity field left
			deleteEntity(id, false);
			return;
		}

		auto [archIndex, archetype] = createArchetype(newTypes);
		if (archetype == oldArchetype)
			return;

		entityDataIndex newElementIndex = archetype->moveFromEntity(id, it->second);
		deleteEntity(id, false);
		setEntityIndexMap(id, newElementIndex);
	}
	
	typeId Ecs::getTypeIdByName(const std::string& typeName)
	{
		auto it = std::find_if(typeDescriptors_.begin(), typeDescriptors_.end(), [&](const std::unique_ptr<TypeDescriptor>& td) -> bool
			{
				return td->name == typeName;
			});

		if (it != typeDescriptors_.end())
			return it->get();

		return nullptr;
	}

	void Ecs::deleteArchetype(int archetypeIndex)
	{
		archetypes_[archetypeIndex].reset();

		while (archetypes_.size() && !archetypes_.back())
			archetypes_.pop_back();
	}

	void Ecs::executeCommmandBuffer()
	{
		//EASY_FUNCTION("executeCommmandBuffer");
		for (auto& itCommand : entityCommandBuffer_)
		{
			itCommand->execute(*this);
			//itCommand.reset();
		}
		entityCommandBuffer_.clear();
		temporaryEntityIdRemapping_.clear();
	}
	
	bool Ecs::lockTypeForRead(typeId t)
	{
		if (auto it = std::find(lockedForWrite.begin(), lockedForWrite.end(), t); it != lockedForWrite.end())
			return false;

		lockedForRead.push_back(t);
		return true;
	}

	bool Ecs::lockTypeForWrite(typeId t)
	{
		if (auto it = std::find(lockedForWrite.begin(), lockedForWrite.end(), t); it != lockedForWrite.end())
			return false;

		if (auto it = std::find(lockedForRead.begin(), lockedForRead.end(), t); it != lockedForRead.end())
			return false;

		lockedForWrite.push_back(t);
		return true;
	}

	void Ecs::releaseTypeForRead(typeId t)
	{
		auto it = std::find(lockedForRead.begin(), lockedForRead.end(), t);
		if (it != lockedForRead.end())
			lockedForRead.erase(it);
		else
			_ASSERT_EXPR(0, "released type is not locked");
	}

	void Ecs::releaseTypeForWrite(typeId t)
	{
		auto it = std::find(lockedForWrite.begin(), lockedForWrite.end(), t);
		if (it != lockedForWrite.end())
			lockedForWrite.erase(it);
		//else
		//	_ASSERT_EXPR(0, "released type is not locked");
	}

	void Ecs::savePrefab(istream& stream, entityId id) const
	{
		auto it = entityDataIndexMap_.find(id);
		if (it == entityDataIndexMap_.end())
			return;

		size_t count = typeDescriptors_.size();
		stream.write((char*)&count, sizeof(size_t));
		for (auto& t : typeDescriptors_)
		{
			stream.write((const char*)&t->index, sizeof(t->index));
			count = t->name.size();
			stream.write((char*)&count, sizeof(size_t));
			stream.write(t->name.data(), count);
		}

	 	auto archetype = archetypes_[it->second.archetypeIndex].get();
		auto typeList = archetype->containedTypes_.createTypeListWithOnlySavedComponents(typeIds_);
		typeList.save(stream);

		archetype->savePrefab(stream, it->second);
	}

	entityId Ecs::createEntityFromPrefabStream(istream& stream)
	{
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

		typeIdList loadedTypeIds = getTypeIds<>();
		loadedTypeIds.load(stream, typeIdsByLoadedIndex);

		entityId newEntityId = nextEntityId++;
		auto [archIndex, archetype] = createArchetype(loadedTypeIds);
		entityDataIndex newIndex = archetype->createEntityFromStream(stream, typeIdsByLoadedIndex, newEntityId);
		setEntityIndexMap(newEntityId, newIndex);
		return newEntityId;
	}

	template<class... Ts>
	void Ecs::savePrefab(istream& stream, const Prefab<Ts...>& prefab)
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

		typeIdList typeIds = getTypeIds<Ts...>();
		typeIds.save(stream);

		typeId prefabTypes[] = { getTypeId<Ts>()... };
		prefab.saveComponents(stream, ComponentType::Regular, prefabTypes, std::index_sequence_for<Ts...>());
		int invalidIndex = -1;
		stream.write((char*)&invalidIndex, sizeof(invalidIndex));

		prefab.saveComponents(stream, ComponentType::Shared, prefabTypes, std::index_sequence_for<Ts...>());
		invalidIndex = -1;
		stream.write((char*)&invalidIndex, sizeof(invalidIndex));
	}

	void Ecs::save(istream& stream) const
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
		typeId dontSaveEntityType = getTypeId<DontSaveEntity>();
		typeId deletedEntity = getTypeId<DeletedEntity>();

		for (size_t iArch = 0; iArch < archetypes_.size(); iArch++)
		{
			if (!archetypes_[iArch] || archetypes_[iArch]->containedTypes_.hasType(dontSaveEntityType))
				skipArchetype[iArch] = 1;
			else if (archetypes_[iArch]->containedTypes_.hasType(dontSaveEntityType))
				skipArchetype[iArch] = 1;
		}

		for (size_t iArch = 0; iArch < archetypes_.size(); iArch++)
		{
			if (skipArchetype[iArch])
				continue;
			auto archetype = archetypes_[iArch].get();
			typeIdList typeIdsToSave = archetype->containedTypes_.createTypeListWithOnlySavedComponents(typeIds_);
			if (typeIdsToSave.isEmpty())
				continue;	// don't save empty archetypes

			std::vector<const Archetype*> archetypesToSave = { archetype };
			for (size_t iArchToCheck = iArch + 1; iArchToCheck < archetypes_.size(); iArchToCheck++)
			{
				if (skipArchetype[iArchToCheck])
					continue;
				auto archetypeToMerge = archetypes_[iArchToCheck].get();
				typeIdList typeIdsToCheck = archetypeToMerge->containedTypes_.createTypeListWithOnlySavedComponents(typeIds_);
				if (typeIdsToSave != typeIdsToCheck)
					continue;

				// These two archetypes are the same without state components
				skipArchetype[iArchToCheck] = 1;
				archetypesToSave.push_back(archetypeToMerge);
			}

			int chunkIndex = 0;
			for (auto arch : archetypesToSave)
			{
				for (auto& chunk : arch->chunks)
				{
					if (!chunk || chunk->size == 0)
						continue;

					auto entityIds = chunk->getEntityIds();
					for (int iEntity = 0; iEntity < chunk->size; iEntity++)
					{
						auto entity = entityIds[iEntity];
						auto& entityMapEntry = entityMapCopy[entity];
						entityMapEntry.archetypeIndex = archetypeIndex;
						entityMapEntry.chunkIndex = chunkIndex;
						entityMapEntry.elementIndex = iEntity;
					}
					chunkIndex++;
				}
			}

			if (chunkIndex == 0)
				continue;

			archetypeIndex++;

			typeIdsToSave.save(stream);

			size_t chunkCount = chunkIndex;
			stream.write((char*)&chunkCount, sizeof(chunkCount));

			for (auto arch : archetypesToSave)
			{
				for (auto& chunk : arch->chunks)
				{
					if (!chunk || chunk->size == 0)
						continue;

					chunk->save(stream);
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
	
	void Ecs::load(istream& stream)
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

			if (loadedTypeIds.isEmpty())
				break;

			auto [archIndex, archetype] = createArchetype(loadedTypeIds);
			archetype->load(stream, typeIdsByLoadedIndex);
		}

		size_t entityCount = 0;
		stream.read((char*)&entityCount, sizeof(entityCount));
		for (size_t i = 0; i < entityCount; i++)
		{
			entityId id;
			stream.read((char*)&id, sizeof(id));
			entityDataIndex dataIndex;
			stream.read((char*)&dataIndex, sizeof(dataIndex));
			setEntityIndexMap(id, dataIndex);
		}

		stream.read((char*)&nextEntityId, sizeof(nextEntityId));
	}
}
