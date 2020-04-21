#pragma once
#include "component_array.h"
#include <algorithm>

namespace ecs
{
	struct Archetype
	{
		Archetype() {}
		Archetype(const typeIdList& typeIds, const ComponentArrayFactory& componentFactory)
		{
			for (auto& t : typeIds)
			{
				addType(t, componentFactory);
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

		template<class T>
		bool add(const ComponentArrayFactory& componentFactory)
		{
			return addType(type_id<T>(), componentFactory);
		}

		bool addType(typeId tid, const ComponentArrayFactory& componentFactory)
		{
			auto it = componentArrays_.find(tid);
			if (it != componentArrays_.end())
				return false;

			componentArrays_[tid] = componentFactory.create(tid);

			containedTypes_.push_back(tid);
			std::sort(containedTypes_.begin(), containedTypes_.end());

			return true;
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

		bool hasAllComponents(const typeQueryList& typeIds) const
		{
			// TODO
			// we need to compare containedTypes and typeIds, both are sorted
			// there is a fast algorithm here, but i just can't get it right
			// for now we'll brute force it
			for (auto& q : typeIds)
			{
				if (q.mode == TypeQueryItem::Mode::Exclude)
				{
					auto it = std::find(containedTypes_.begin(), containedTypes_.end(), q.type);
					if (it != containedTypes_.end())
						return false;
				}
				else
				{
					auto it = std::find(containedTypes_.begin(), containedTypes_.end(), q.type);
					if (it == containedTypes_.end())
						return false;
				}
			}
			return true;
		}

		typeIdList containedTypes_;
		std::vector<entityId> entityIds_;
		std::unordered_map<typeId, std::unique_ptr<ComponentArrayBase>> componentArrays_;
	};
}
