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

		ComponentArrayBase* get_(typeId tid);
		bool hasAllComponents(const typeQueryList& query) const;

		std::tuple<Chunk*, int> getOrCreateChunkForNewEntity();

		void save(std::ostream& stream) const;
		void load(std::istream& stream, const typeIdList& typeIds, const std::vector<typeId>& allRegisterTypeIds, const ComponentArrayFactory& componentFactory);

		typeIdList containedTypes_;
		std::vector<entityId> entityIds_;	// TODO this needs to become a component maybe, but we need efficient queries by this
		std::vector<std::unique_ptr<Chunk>> chunks; // TODO this needs to be a linked list of chunks most likely
		Ecs* ecs = nullptr;
		int archetypeIndex;
	};
}
