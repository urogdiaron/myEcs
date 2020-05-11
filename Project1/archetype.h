#pragma once
#include "component_array.h"
#include <algorithm>

namespace ecs
{
	struct Archetype
	{
		Archetype() : containedTypes_(1, {}) {}

		Archetype(const typeIdList& typeIds, const ComponentArrayFactory& componentFactory)
			: containedTypes_(typeIds)
		{
			for (auto& tid : typeIds.getTypeIds())
			{
				componentArrays_[tid] = componentFactory.create(tid);
			}
		}

		ComponentArrayBase* get(typeId tid)
		{
			auto it = componentArrays_.find(tid);
			if (it != componentArrays_.end())
			{
				return it->second.get();
			}
			return nullptr;
		}

		int createEntity(entityId id)
		{
			int newIndex = (int)entityIds_.size();
			entityIds_.push_back(id);
			for (auto& it : componentArrays_)
			{
				it.second->createEntity();
			}

			return newIndex;
		}

		void deleteEntity(int elementIndex)
		{
			deleteFromVectorUnsorted(entityIds_, elementIndex);
			for (auto& it : componentArrays_)
			{
				it.second->deleteEntity(elementIndex);
			}
		}

		int copyFromEntity(entityId id, int sourceElementIndex, const Archetype* sourceArchetype)
		{
			for (auto& itDestArray : componentArrays_)
			{
				auto itSourceArray = sourceArchetype->componentArrays_.find(itDestArray.second->getTypeId());
				if (itSourceArray == sourceArchetype->componentArrays_.end())
				{
					itDestArray.second->createEntity();
				}
				else
				{
					itDestArray.second->copyFromArray(sourceElementIndex, itSourceArray->second.get());
				}
			}

			int newIndex = (int)entityIds_.size();
			entityIds_.push_back(id);
			return newIndex;
		}

		bool hasAllComponents(const typeQueryList& query) const
		{
			return query.check(containedTypes_);
		}

		void save(std::ostream& stream) const
		{
			containedTypes_.save(stream);
			size_t count = 0;
			count = entityIds_.size();
			stream.write((const char*)&count, sizeof(count));
			stream.write((const char*)entityIds_.data(), entityIds_.size() * sizeof(entityId));

			count = componentArrays_.size();
			stream.write((const char*)&count, sizeof(count));
			for (auto& it : componentArrays_)
			{
				stream.write((const char*)&it.first->index, sizeof(typeIndex));
				it.second->save(stream);
			}
		}

		void load(std::istream& stream, const typeIdList& typeIds, const std::vector<typeId>& typeIdsByLoadedIndex, const ComponentArrayFactory& componentFactory)
		{
			containedTypes_ = typeIds;
			size_t entityCount = 0;
			stream.read((char*)&entityCount, sizeof(entityCount));
			entityIds_.resize(entityCount);
			stream.read((char*)entityIds_.data(), entityCount * sizeof(entityId));

			auto typesSortedByIndex = containedTypes_.getTypeIds();
			std::sort(typesSortedByIndex.begin(), typesSortedByIndex.end(), [](const typeId& a, const typeId& b) -> int
				{
					return a->index < b->index;
				});

			for (auto& t : typesSortedByIndex)
			{
				auto componentArray = componentFactory.create(t);
				componentArray->load(stream, entityCount);
				componentArrays_[t] = std::move(componentArray);
			}
		}

		typeIdList containedTypes_;
		std::vector<entityId> entityIds_;
		std::unordered_map<typeId, std::unique_ptr<ComponentArrayBase>> componentArrays_;
	};
}
