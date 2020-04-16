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
			int queryIndex = 0;
			int containedTypeIndex = 0;
			int foundCount = 0;

			while (true)
			{
				if (queryIndex >= (int)typeIds.size())
					break;

				if (containedTypeIndex >= (int)containedTypes_.size())
					break;

				if (typeIds[queryIndex].mode == TypeQueryItem::Exclude)
				{
					if (containedTypes_[containedTypeIndex] == typeIds[queryIndex].type)
					{
						return false;
					}
					else if (containedTypes_[containedTypeIndex] > typeIds[queryIndex].type)
					{
						queryIndex++;
					}
				}
				else
				{
					if (containedTypes_[containedTypeIndex] == typeIds[queryIndex].type)
					{
						queryIndex++;
						containedTypeIndex++;
						foundCount++;
					}
					else
					{
						containedTypeIndex++;
					}
				}
			}

			return foundCount == typeIds.size();
		}

		typeIdList containedTypes_;
		std::vector<entityId> entityIds_;
		std::unordered_map<typeId, std::unique_ptr<ComponentArrayBase>> componentArrays_;
	};
}
