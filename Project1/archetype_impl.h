#pragma once
#include "component_array.h"
#include "archetype.h"
#include "ecs.h"
#include <algorithm>


namespace ecs
{
	Archetype::Archetype()
		: containedTypes_(1, {})
	{}
	
	Archetype::Archetype(const typeIdList& typeIds, Ecs* ecs)
		: containedTypes_(typeIds)
		, chunk(this, typeIds.calcTypeIds(ecs->typeIds_), ecs->componentArrayFactory_)
	{
	}
	
	ComponentArrayBase* Archetype::get(typeId tid)
	{
		return chunk.getArray(tid);
	}
	
	int Archetype::createEntity(entityId id)
	{
		int newIndex = (int)entityIds_.size();
		entityIds_.push_back(id);
		chunk.createEntity(id);

		return newIndex;
	}
	
	void Archetype::deleteEntity(int elementIndex)
	{
		deleteFromVectorUnsorted(entityIds_, elementIndex);
		chunk.deleteEntity(elementIndex);
	}
	
	int Archetype::copyFromEntity(entityId id, int sourceElementIndex, Archetype* sourceArchetype)
	{
		chunk.moveEntityFromOtherChunk(&sourceArchetype->chunk, sourceElementIndex);
		int newIndex = (int)entityIds_.size();
		entityIds_.push_back(id);
		return newIndex;
	}
	
	bool Archetype::hasAllComponents(const typeQueryList& query) const
	{
		return query.check(containedTypes_);
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
}
