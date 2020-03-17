#pragma once
#include "ecs_util.h"
#include "self_register.h"

namespace ecs
{
	struct ComponentArrayBase
	{
		virtual ~ComponentArrayBase() {}
		virtual typeId getTypeId() const = 0;
		virtual void createEntity() = 0;
		virtual void deleteEntity(int elementIndex) = 0;
		virtual void copyFromArray(int sourceElementIndex, const ComponentArrayBase* sourceArray) = 0;
	};

	using ComponentFactory = self_registering::FactoryMap<ComponentArrayBase, typeId>;

	template<class T>
	using ComponentRegister = self_registering::SelfRegistering<T, ComponentArrayBase, typeId>;


	template<class T>
	struct ComponentArray : public ComponentArrayBase, public ComponentRegister<ComponentArray<T>>
	{
		static typeId getKey() { return type_id<T>(); }

		typeId getTypeId() const override { return type_id<T>(); }

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
}