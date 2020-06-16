#pragma once
#include "component_array.h"
#include "archetype.h"
#include "ecs.h"
#include <algorithm>


namespace ecs
{
	Archetype::Archetype()
		: containedTypes_(1, {})
		, archetypeIndex(-1)
	{}
	
	Archetype::Archetype(const typeIdList& typeIds, int archetypeIndex, Ecs* ecs)
		: containedTypes_(typeIds)
		, archetypeIndex(archetypeIndex)
		, ecs(ecs)
	{
	}

	void Archetype::deleteChunk(int chunkIndex)
	{
		chunks[chunkIndex].reset();

		while (chunks.size() && !chunks.back())
			chunks.pop_back();
	}
	
	entityDataIndex Archetype::createEntity(entityId id)
	{
		entityDataIndex ret;
		ret.archetypeIndex = archetypeIndex;
		auto chunk = getOrCreateChunkForNewEntity();
		ret.chunkIndex = currentlyFilledChunkIndex;
		ret.elementIndex = chunk->createEntity(id);
		return ret;
	}
	
	entityId Archetype::deleteEntity(const entityDataIndex& index)
	{
		_ASSERT(index.archetypeIndex == archetypeIndex);
		Chunk* chunk = chunks[index.chunkIndex].get();
		entityId movedEntityId = chunk->deleteEntity(index.elementIndex);
		if (chunk->size == 0)
		{
			if (movedEntityId) 
				printf("Deleting a chunk when we move an entity into it\n");
			deleteChunk(index.chunkIndex);
		}
		return movedEntityId;
	}
	
	entityDataIndex Archetype::moveFromEntity(entityId id, const entityDataIndex& sourceIndex)
	{
		Archetype* sourceArchetype = ecs->archetypes_[sourceIndex.archetypeIndex].get();
		Chunk* sourceChunk = sourceArchetype->chunks[sourceIndex.chunkIndex].get();

		entityDataIndex ret;
		ret.archetypeIndex = archetypeIndex;
		auto [chunk, chunkIndex] = getOrCreateChunkForMovedEntity(sourceIndex);
		ret.chunkIndex = chunkIndex;
		ret.elementIndex = chunk->moveEntityFromOtherChunk(sourceChunk, sourceIndex.elementIndex);
		return ret;
	}
	
	bool Archetype::hasAllComponents(const typeQueryList& query) const
	{
		return query.check(containedTypes_);
	}

	Chunk* Archetype::getOrCreateChunkForNewEntity()
	{
		Chunk* chunkForNewEntity = nullptr;
		if (currentlyFilledChunkIndex >= 0 && currentlyFilledChunkIndex < (int)chunks.size() && chunks[currentlyFilledChunkIndex])
		{
			chunkForNewEntity = chunks[currentlyFilledChunkIndex].get();
			if (chunkForNewEntity->size < chunkForNewEntity->entityCapacity)
				return chunkForNewEntity;
		}

		// The currently used chunk index is not good, let's change it
		// Let's try to reuse an index
		for (int iChunk = 0; iChunk < (int)chunks.size(); iChunk++)
		{
			if (!chunks[iChunk])
			{
				chunks[iChunk] = std::make_unique<Chunk>(this, containedTypes_.calcTypeIds(ecs->typeIds_), ecs->componentArrayFactory_);
				currentlyFilledChunkIndex = iChunk;
				return chunks[currentlyFilledChunkIndex].get();
			}
		}

		auto& retPtr = chunks.emplace_back(
			std::make_unique<Chunk>(this, containedTypes_.calcTypeIds(ecs->typeIds_), ecs->componentArrayFactory_)
		);

		currentlyFilledChunkIndex = (int)chunks.size() - 1;
		return retPtr.get();

	}

	std::tuple<Chunk*, int> Archetype::getOrCreateChunkForMovedEntity(entityDataIndex currentIndex)
	{
		Chunk* currentChunk = ecs->archetypes_[currentIndex.archetypeIndex]->chunks[currentIndex.chunkIndex].get();

		Chunk* newChunk = nullptr;
		int newChunkIndex = -1;
		for (int iChunk = 0; iChunk < (int)chunks.size(); iChunk++)
		{
			Chunk* destChunk = chunks[iChunk].get();
			if (!destChunk)
				continue;

			bool allSharedComponentsAreEqual = true;
			for (int iSharedType = 0; iSharedType < (int)destChunk->sharedComponents.size(); iSharedType++)
			{
				auto destArray = destChunk->sharedComponents[iSharedType].get();
				auto srcArray = currentChunk->getSharedComponentArray(destArray->tid);
				if (!srcArray)
					continue;

				if (!destArray->isSameAsSharedComponent(srcArray))
				{
					allSharedComponentsAreEqual = false;
					break;
				}
			}

			if (!allSharedComponentsAreEqual)
				continue;

			if (destChunk->entityCapacity > destChunk->size)
			{
				newChunk = destChunk;
				newChunkIndex = iChunk;
				break;
			}
		}

		if (newChunk == nullptr)
		{
			for (int iChunk = 0; iChunk < (int)chunks.size(); iChunk++)
			{
				if (!chunks[iChunk])
				{
					chunks[iChunk] = std::make_unique<Chunk>(this, containedTypes_.calcTypeIds(ecs->typeIds_), ecs->componentArrayFactory_);
					newChunk = chunks[iChunk].get();
					newChunkIndex = iChunk;
				}
			}

			if (!newChunk)
			{
				auto& retPtr = chunks.emplace_back(
					std::make_unique<Chunk>(this, containedTypes_.calcTypeIds(ecs->typeIds_), ecs->componentArrayFactory_)
				);
				newChunk = retPtr.get();
				newChunkIndex = (int)chunks.size() - 1;
			}

			// Copy all other shared component from the current chunk to the new one
			for (int iSharedType = 0; iSharedType < (int)newChunk->sharedComponents.size(); iSharedType++)
			{
				auto destArray = newChunk->sharedComponents[iSharedType].get();
				auto srcArray = currentChunk->getSharedComponentArray(destArray->tid);
				destArray->copyFromArray(0, srcArray, 0);
			}
		}

		return { newChunk, newChunkIndex };
	}
	
	void Archetype::save(std::ostream& stream) const
	{
		size_t chunkCount = chunks.size();
		stream.write((char*)&chunkCount, sizeof(chunkCount));
		for (int iChunk = 0; iChunk < chunkCount; iChunk++)
		{
			chunks[iChunk]->save(stream);
		}
	}
	
	void Archetype::load(std::istream& stream, const std::vector<typeId>& typeIdsByLoadedIndex)
	{
		size_t chunkCount = 0;
		stream.read((char*)&chunkCount, sizeof(chunkCount));
		for (int iChunk = 0; iChunk < chunkCount; iChunk++)
		{
			auto chunk = chunks.emplace_back(
				std::make_unique<Chunk>(this, containedTypes_.calcTypeIds(ecs->typeIds_), ecs->componentArrayFactory_)
			).get();

			chunk->load(stream, typeIdsByLoadedIndex);
		}
	}

	void Archetype::savePrefab(std::ostream& stream, entityDataIndex entityIndex) const
	{
		auto chunk = chunks[entityIndex.chunkIndex].get();
		chunk->saveElement(stream, entityIndex.elementIndex);

		for (auto& sharedComponentArray : chunk->sharedComponents)
		{
			int componentIndex = sharedComponentArray->tid->index;
			stream.write((char*)&componentIndex, sizeof(componentIndex));

			ComponentData sharedComponentData = sharedComponentArray->getElementData(entityIndex.elementIndex);
			stream.write((char*)sharedComponentData.data, sizeof(sharedComponentArray->tid->size));
		}

		int invalidComponentIndex = -1;
		stream.write((char*)&invalidComponentIndex, sizeof(invalidComponentIndex));
	}

	entityDataIndex Archetype::createEntityFromStream(std::istream& stream, const std::vector<typeId>& typeIdsByLoadedIndex, entityId id)
	{
		entityDataIndex loadedEntityIndex;
		loadedEntityIndex.archetypeIndex = archetypeIndex;
		auto chunk = getOrCreateChunkForNewEntity();
		loadedEntityIndex.chunkIndex = currentlyFilledChunkIndex;
		loadedEntityIndex.elementIndex = chunk->createEntity(id);

		chunk->loadElement(stream, typeIdsByLoadedIndex, loadedEntityIndex.elementIndex);
		ecs->setEntityIndexMap(id, loadedEntityIndex);

		while (true)
		{
			int componentIndex;
			stream.read((char*)&componentIndex, sizeof(componentIndex));
			if (componentIndex < 0)
				break;

			typeId componentTypeId = typeIdsByLoadedIndex[componentIndex];
			std::vector<uint8_t> sharedComponentData(componentTypeId->size);
			stream.read((char*)sharedComponentData.data(), componentTypeId->size);

			auto [currentEntityIndex, movedEntityId] = setSharedComponent(loadedEntityIndex, { { componentTypeId, (void*)sharedComponentData.data() } });
			ecs->setEntityIndexMap(id, currentEntityIndex);
			if(movedEntityId)
				ecs->setEntityIndexMap(movedEntityId, loadedEntityIndex);

			loadedEntityIndex = currentEntityIndex;
		}
		return loadedEntityIndex;
	}

	template<class T>
	std::tuple<entityDataIndex, entityId> Archetype::setSharedComponent(entityDataIndex currentIndex, const T& newSharedComponentValue)
	{
		typeId sharedType = ecs->getTypeId<T>();
		return setSharedComponent(currentIndex, tempList<ComponentData>{ ComponentData{ sharedType, (void*)&newSharedComponentValue } });
	}

	std::tuple<entityDataIndex, entityId> Archetype::setSharedComponent(entityDataIndex currentIndex, const tempList<ComponentData>& newSharedComponentDatas)
	{
		Chunk* currentChunk = chunks[currentIndex.chunkIndex].get();

		bool noSharedDataChanged = true;
		for(auto& newData : newSharedComponentDatas)
		{
			typeId sharedComponentType = newData.tid;
			ComponentData originalSharedComponentData = currentChunk->getSharedComponentData(sharedComponentType);
			if (!newData.equals(originalSharedComponentData))
			{
				noSharedDataChanged = false;
				break;
			}
		}

		if (noSharedDataChanged)
		{
			return { currentIndex, 0 };
		}

		// we need to get the chunk this belongs to or create a new one
		Chunk* newChunk = nullptr;
		int newChunkIndex = -1;
		for (int iChunk = 0; iChunk < (int)chunks.size(); iChunk++)
		{
			Chunk* c = chunks[iChunk].get();
			if (!c) continue;

			bool allNewDataIsFound = true;

			for (auto& newData : newSharedComponentDatas)
			{
				const ComponentData sharedData = c->getSharedComponentData(newData.tid);
				if (!sharedData.equals(newData))
				{
					allNewDataIsFound = false;
					break;
				}
			}

			if (allNewDataIsFound)
			{
				// Check all the other shared values
				bool allSharedComponentsAreEqual = true;
				for (int iSharedType = 0; iSharedType < (int)currentChunk->sharedComponents.size(); iSharedType++)
				{
					auto componentToCheck = c->sharedComponents[iSharedType].get();

					bool alreadyChecked = false;
					for (auto& newData : newSharedComponentDatas)
					{
						if (newData.tid == componentToCheck->tid)
						{
							alreadyChecked = true;
							break;
						}
					}

					if (alreadyChecked)
						continue;

					if (!componentToCheck->isSameAsSharedComponent(currentChunk->sharedComponents[iSharedType].get()))
					{
						allSharedComponentsAreEqual = false;
						break;
					}
				}

				if (!allSharedComponentsAreEqual)
					continue;

				if (c->entityCapacity > c->size)
				{
					newChunk = c;
					newChunkIndex = iChunk;
					break;
				}
			}
		}

		if (newChunk == nullptr)
		{
			auto& newChunkPtr = chunks.emplace_back(
				std::make_unique<Chunk>(this, containedTypes_.calcTypeIds(ecs->typeIds_), ecs->componentArrayFactory_)
			);

			// Copy all other shared component from the current chunk to the new one
			for (int iSharedType = 0; iSharedType < (int)currentChunk->sharedComponents.size(); iSharedType++)
			{
				auto componentToCopy = currentChunk->sharedComponents[iSharedType].get();

				bool hasNewData = false;
				for (auto& newData : newSharedComponentDatas)
				{
					if (newData.tid == componentToCopy->tid)
					{
						hasNewData = true;
						break;
					}
				}

				if (hasNewData)
					continue;

				// Didnt specify new data, copy from the old one
				newChunkPtr->sharedComponents[iSharedType]->copyFromArray(0, componentToCopy, 0);
			}

			// Set the new data from the shared components
			for (auto& newData : newSharedComponentDatas)
			{
				ComponentData sharedValue = newChunkPtr->getSharedComponentData(newData.tid);
				memcpy(sharedValue.data, newData.data, newData.tid->size);
			}
			newChunk = newChunkPtr.get();
			newChunkIndex = (int)chunks.size() - 1;
		}

		int newElementIndex = newChunk->moveEntityFromOtherChunk(currentChunk, currentIndex.elementIndex);
		entityId movedEntityId = currentChunk->deleteEntity(currentIndex.elementIndex);
		if (currentChunk->size == 0)
		{
			_ASSERT_EXPR(movedEntityId == 0, "We moved an entity into a chunk that's empty");
			deleteChunk(currentIndex.chunkIndex);
		}

		entityDataIndex newEntityIndex;
		newEntityIndex.archetypeIndex = currentIndex.archetypeIndex;
		newEntityIndex.chunkIndex = newChunkIndex;
		newEntityIndex.elementIndex = newElementIndex;

		return { newEntityIndex, movedEntityId };
	}
}
