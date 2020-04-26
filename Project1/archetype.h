#pragma once
#include "component_array.h"
#include <algorithm>

namespace ecs
{
	struct Archetype
	{
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
			EASY_FUNCTION();
			return query.check(containedTypes_);
		}

		typeIdList containedTypes_;
		std::vector<entityId> entityIds_;
		std::unordered_map<typeId, std::unique_ptr<ComponentArrayBase>> componentArrays_;
	};
}
