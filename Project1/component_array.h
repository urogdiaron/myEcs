#pragma once
#include "ecs_util.h"
#include <functional>

namespace ecs
{
	struct ComponentArrayBase
	{
		ComponentArrayBase(typeId tid) : tid(tid) {}
		virtual ~ComponentArrayBase() {}
		virtual void createEntity() = 0;
		virtual void deleteEntity(int elementIndex) = 0;
		virtual void copyFromArray(int sourceElementIndex, const ComponentArrayBase* sourceArray) = 0;
		typeId getTypeId() { return tid; }
	private:
		typeId tid;
	};

	template<class T>
	struct ComponentArray : public ComponentArrayBase
	{
		ComponentArray(typeId tid) : ComponentArrayBase(tid) {}

		void createEntity() override
		{
			components_.push_back(T{});
		}

		void deleteEntity(int elementIndex)
		{
			deleteFromVectorUnsorted(components_, elementIndex);
		}

		void copyFromArray(int sourceElementIndex, const ComponentArrayBase* sourceArray)
		{
			auto sourceArrayCasted = static_cast<const ComponentArray<T>*>(sourceArray);
			components_.push_back(sourceArrayCasted->components_[sourceElementIndex]);
		}

		std::vector<T> components_;
	};

	struct ComponentArrayFactory
	{
		std::unique_ptr<ComponentArrayBase> create(const typeId& componentId) const
		{
			auto it = factoryFunctions.find(componentId);
			if (it == factoryFunctions.end())
				return nullptr;
			return it->second();
		}

		template<class T>
		void addFactoryFunction(const typeId& componentId)
		{
			std::function<std::unique_ptr<ComponentArrayBase>()> fn;
			fn = [componentId]()
			{
				return std::make_unique<ComponentArray<T>>(componentId);
			};

			factoryFunctions[componentId] = fn;
		}

		std::unordered_map<typeId, std::function<std::unique_ptr<ComponentArrayBase>()>> factoryFunctions;
	};
}