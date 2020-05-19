#pragma once
#include "component_array.h"

namespace ecs
{
	struct Ecs;
	struct Archetype
	{
		Archetype();
		Archetype(const typeIdList& typeIds, Ecs* ecs);

		int createEntity(entityId id);
		void deleteEntity(int elementIndex);
		int copyFromEntity(entityId id, int sourceElementIndex, Archetype* sourceArchetype);

		ComponentArrayBase* get(typeId tid);
		bool hasAllComponents(const typeQueryList& query) const;

		void save(std::ostream& stream) const;
		void load(std::istream& stream, const typeIdList& typeIds, const std::vector<typeId>& allRegisterTypeIds, const ComponentArrayFactory& componentFactory);

		typeIdList containedTypes_;
		std::vector<entityId> entityIds_;	// TODO this needs to become a component maybe, but we need efficient queries by this
		Chunk chunk; // TODO this needs to be a linked list of chunks most likely
		Ecs* ecs = nullptr;
	};
}
