#pragma once
#include "component_array.h"
#include <algorithm>

namespace ecs
{
	struct Archetype
	{
		Archetype() {}
		Archetype(const std::vector<typeId>& typeIds)
		{
			for (auto& t : typeIds)
			{
				addType(t);
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
		bool add()
		{
			return addType(type_id<T>());
		}

		bool addType(typeId tid)
		{
			auto it = componentArrays_.find(tid);
			if (it != componentArrays_.end())
				return false;

			componentArrays_[tid] = ComponentFactory::create(tid);

			containedTypes_.push_back(tid);
			std::sort(containedTypes_.begin(), containedTypes_.end());

			return true;
		}

		int createEntity(entityId id)
		{
			int newIndex = entityIds_.size();
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

			int newIndex = entityIds_.size();
			entityIds_.push_back(id);
			return newIndex;
		}

		bool hasAllComponents(const std::vector<typeId>& typeIds) const
		{
			int requiredTypeIndex = 0;
			int containedTypeIndex = 0;
			int foundCount = 0;

			while (true)
			{
				if (requiredTypeIndex >= (int)typeIds.size())
					break;

				if (containedTypeIndex >= (int)containedTypes_.size())
					break;

				if (containedTypes_[containedTypeIndex] == typeIds[requiredTypeIndex])
				{
					requiredTypeIndex++;
					containedTypeIndex++;
					foundCount++;
				}
				else
				{
					containedTypeIndex++;
				}
			}

			return foundCount == typeIds.size();
		}

		std::vector<typeId> containedTypes_;
		std::vector<entityId> entityIds_;
		std::unordered_map<typeId, std::unique_ptr<ComponentArrayBase>> componentArrays_;
	};
}
