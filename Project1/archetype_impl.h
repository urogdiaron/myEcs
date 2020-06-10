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

	template<class T>
	std::tuple<entityDataIndex, entityId> Archetype::setSharedComponent(entityId id, entityDataIndex currentIndex, const T& newSharedComponentValue)
	{
		Chunk* currentChunk = chunks[currentIndex.chunkIndex].get();
		typeId sharedComponentType = ecs->getTypeId<T>();
		const T* originalSharedComponent = currentChunk->getSharedComponent<T>(sharedComponentType);
		if (equals(*originalSharedComponent, newSharedComponentValue))
		{	// no need to change anything
			return { currentIndex, 0 };
		}

		// we need to get the chunk this belongs to or create a new one
		Chunk* newChunk = nullptr;
		int newChunkIndex = -1;
		for (int iChunk = 0; iChunk < (int)chunks.size(); iChunk++)
		{
			Chunk* c = chunks[iChunk].get();
			if (!c) continue;

			const T* sharedValue = c->getSharedComponent<T>(sharedComponentType);
			if (equals(*sharedValue, newSharedComponentValue))
			{
				// Check all the other shared values
				bool allSharedComponentsAreEqual = true;
				for (int iSharedType = 0; iSharedType < (int)currentChunk->sharedComponents.size(); iSharedType++)
				{
					auto componentToCheck = c->sharedComponents[iSharedType].get();
					if (componentToCheck->tid == sharedComponentType)
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
				if (componentToCopy->tid == sharedComponentType)
					continue;

				newChunkPtr->sharedComponents[iSharedType]->copyFromArray(0, componentToCopy, 0);
			}

			T* sharedValue = newChunkPtr->getSharedComponent<T>(sharedComponentType);
			*sharedValue = newSharedComponentValue;
			newChunk = newChunkPtr.get();
			newChunkIndex = (int)chunks.size() - 1;
		}

		int newElementIndex = newChunk->moveEntityFromOtherChunk(currentChunk, currentIndex.elementIndex, sharedComponentType);
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
