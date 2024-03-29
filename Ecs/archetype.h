#pragma once
#include "component_array.h"

namespace ecs
{
	struct Ecs;
	struct Archetype
	{
		Archetype();
		Archetype(const typeIdList& typeIds, int archetypeIndex, Ecs* ecs);

		entityDataIndex createEntity(entityId id);
		entityId deleteEntity(const entityDataIndex& index); // returns the entity that moved to this index (can be invalid)
		entityDataIndex moveFromEntity(entityId id, const entityDataIndex& sourceIndex); // source will become invalid but won't get deleted
		entityDataIndex allocateEntity(const tempList<ComponentData>& sharedComponentDatas);

		ComponentArrayBase* get_(typeId tid);
		bool hasAllComponents(const typeQueryList& query) const;

		Chunk* getOrCreateChunkForNewEntity();
		std::tuple<Chunk*, int> getOrCreateChunkForNewEntity(const tempList<ComponentData>& sharedComponentDatas);

		std::tuple<Chunk*, int> getOrCreateChunkForMovedEntity(entityDataIndex currentIndex);
		// return the new entityDataIndex of the entity and the entityId that moved to its original place
		template<class T>
		std::tuple<entityDataIndex, entityId> setSharedComponent(entityDataIndex currentIndex, const T& sharedComponentValue);

		std::tuple<entityDataIndex, entityId> setSharedComponent(entityDataIndex currentIndex, const tempList<ComponentData>& newSharedComponentDatas);

		void save(istream& stream) const;
		void load(istream& stream, const std::vector<typeId>& typeIdsByLoadedIndex);
		void savePrefab(istream& stream, entityDataIndex entityIndex) const;
		entityDataIndex createEntityFromStream(istream& stream, const std::vector<typeId>& typeIdsByLoadedIndex, entityId id);

	private:
		std::tuple<Chunk*, int> createChunk();
		void deleteChunk(int chunkIndex);

	public:
		typeIdList containedTypes_;
		std::vector<std::unique_ptr<Chunk>> chunks; // TODO this needs to be a linked list of chunks most likely
		Ecs* ecs = nullptr;
		int archetypeIndex;
		int currentlyFilledChunkIndex = -1;
	};
}
