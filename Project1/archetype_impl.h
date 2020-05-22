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
	
	ComponentArrayBase* Archetype::get_(typeId tid)
	{
		return nullptr;// chunk.getArray(tid);
	}
	
	entityDataIndex Archetype::createEntity(entityId id)
	{
		entityDataIndex ret;
		ret.archetypeIndex = archetypeIndex;
		auto [chunk, chunkIndex] = getOrCreateChunkForNewEntity();
		ret.chunkIndex = chunkIndex;
		ret.elementIndex = chunk->createEntity(id);
		return ret;
	}
	
	entityId Archetype::deleteEntity(const entityDataIndex& index)
	{
		_ASSERT(index.archetypeIndex == archetypeIndex);
		Chunk* chunk = chunks[index.chunkIndex].get();
		entityId movedEntityId = chunk->deleteEntity(index.elementIndex);
		return movedEntityId;
	}
	
	entityDataIndex Archetype::moveFromEntity(entityId id, const entityDataIndex& sourceIndex)
	{
		Archetype* sourceArchetype = ecs->archetypes_[sourceIndex.archetypeIndex].get();
		Chunk* sourceChunk = sourceArchetype->chunks[sourceIndex.chunkIndex].get();

		entityDataIndex ret;
		ret.archetypeIndex = archetypeIndex;
		auto [chunk, chunkIndex] = getOrCreateChunkForNewEntity();
		ret.chunkIndex = chunkIndex;
		ret.elementIndex = chunk->moveEntityFromOtherChunk(sourceChunk, sourceIndex.elementIndex);
		return ret;
	}
	
	bool Archetype::hasAllComponents(const typeQueryList& query) const
	{
		return query.check(containedTypes_);
	}

	std::tuple<Chunk*, int> Archetype::getOrCreateChunkForNewEntity()
	{
		int chunkIndex = (int)chunks.size() - 1;
		if (chunkIndex < 0 || 
			(chunks[chunkIndex]->entityCapacity == chunks[chunkIndex]->size))
		{
			auto& retPtr = chunks.emplace_back(
				std::make_unique<Chunk>(this, containedTypes_.calcTypeIds(ecs->typeIds_), ecs->componentArrayFactory_)
			);
			return { retPtr.get(), chunkIndex + 1 };
		}

		return { chunks[chunkIndex].get(), chunkIndex };
	}
	
	void Archetype::save(std::ostream& stream) const
	{
		//containedTypes_.save(stream);
		//size_t count = 0;
		//count = entityIds_.size();
		//stream.write((const char*)&count, sizeof(count));
		//stream.write((const char*)entityIds_.data(), entityIds_.size() * sizeof(entityId));
		//
		//count = componentArrays_.size();
		//stream.write((const char*)&count, sizeof(count));
		//for (auto& it : componentArrays_)
		//{
		//	stream.write((const char*)&it.first->index, sizeof(typeIndex));
		//	it.second->save(stream);
		//}
	}
	
	void Archetype::load(std::istream& stream, const typeIdList& typeIds, const std::vector<typeId>& allRegisterTypeIds, const ComponentArrayFactory& componentFactory)
	{
		//containedTypes_ = typeIds;
		//size_t entityCount = 0;
		//stream.read((char*)&entityCount, sizeof(entityCount));
		//entityIds_.resize(entityCount);
		//stream.read((char*)entityIds_.data(), entityCount * sizeof(entityId));
		//
		//auto typesSortedByIndex = containedTypes_.calcTypeIds(allRegisterTypeIds);
		//for (auto& t : typesSortedByIndex)
		//{
		//	auto componentArray = componentFactory.create(t);
		//	componentArray->load(stream, entityCount);
		//	componentArrays_[t] = std::move(componentArray);
		//}
	}

	template<class T>
	entityDataIndex Archetype::setSharedComponent(entityId id, entityDataIndex currentIndex, const T& newSharedComponentValue)
	{
		Chunk* currentChunk = chunks[currentIndex.chunkIndex].get();
		typeId sharedComponentType = ecs->getTypeId<T>();
		const T* originalSharedComponent = currentChunk->getSharedComponent<T>(sharedComponentType);
		if (equals(*originalSharedComponent, newSharedComponentValue))
		{	// no need to change anything
			return currentIndex;
		}

		// we need to get the chunk this belongs to or create a new one
		Chunk* newChunk = nullptr;
		int newChunkIndex = -1;
		for (int iChunk = 0; iChunk < (int)chunks.size(); iChunk++)
		{
			Chunk* c = chunks[iChunk].get();
			const T* sharedValue = c->getSharedComponent<T>(sharedComponentType);
			if (equals(*sharedValue, newSharedComponentValue))
			{
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
			T* sharedValue = newChunkPtr->getSharedComponent<T>(sharedComponentType);
			*sharedValue = newSharedComponentValue;
			newChunk = newChunkPtr.get();
			newChunkIndex = (int)chunks.size() - 1;
		}

		int newElementIndex = newChunk->moveEntityFromOtherChunk(currentChunk, currentIndex.elementIndex);
		entityDataIndex ret;
		ret.archetypeIndex = currentIndex.archetypeIndex;
		ret.chunkIndex = newChunkIndex;
		ret.elementIndex = newElementIndex;

		return ret;
	}
}
