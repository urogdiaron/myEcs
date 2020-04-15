#pragma once
#include "ecs.h"

namespace ecs
{
	struct EntityCommand
	{
		virtual void execute(struct Ecs& ecs) = 0;
	};

	template<class ...Ts>
	struct EntityCommand_Create : EntityCommand
	{
		EntityCommand_Create(entityId id, const Ts&... initialValues)
			: temporaryId(id)
		{
			args = std::make_tuple(initialValues...);
		}

		void execute(Ecs& ecs) override
		{
			entityId newId = std::apply([&](auto... x) { return ecs.createEntity<Ts...>(x...); }, args);
			ecs.temporaryEntityIdRemapping_[temporaryId] = newId;
		}

		entityId temporaryId;
		std::tuple<Ts...> args;
	};

	struct EntityCommand_Delete : EntityCommand
	{
		EntityCommand_Delete(entityId id)
			: id(id)
		{}

		void execute(struct Ecs& ecs) override 
		{
			if (id < 0)
			{
				id = ecs.temporaryEntityIdRemapping_[id];
			}

			ecs.deleteEntity(id);
		}

		entityId id;
	};

	template<class T>
	struct EntityCommand_SetComponent : EntityCommand
	{
		EntityCommand_SetComponent(entityId id, const T& data)
			: id(id)
			, data(data)
		{}

		void execute(struct Ecs& ecs) override
		{
			if (id < 0)
				id = ecs.temporaryEntityIdRemapping_[id];

			auto comp = ecs.getComponent<T>(id);
			if (comp)
				*comp = data;
			else
				printf("EntityCommand_SetComponent: Component data not found. Id: %d; Type: %s.", id, ecs.getTypeId<T>()->name.c_str());
		}

		entityId id;
		T data;
	};

	struct EntityCommand_DeleteComponents : EntityCommand
	{
		EntityCommand_DeleteComponents(entityId id, const typeIdList& types)
			: id(id)
			, types(types)
		{}

		void execute(struct Ecs& ecs) override
		{
			if (id < 0)
				id = ecs.temporaryEntityIdRemapping_[id];

			ecs.deleteComponents(id, types);
		}

		entityId id;
		typeIdList types;
	};

	struct EntityCommand_ChangeComponents : EntityCommand
	{
		EntityCommand_ChangeComponents(entityId id, const typeIdList& types)
			: id(id)
			, types(types)
		{}

		void execute(struct Ecs& ecs) override
		{
			if (id < 0)
				id = ecs.temporaryEntityIdRemapping_[id];

			ecs.changeComponents(id, types);
		}

		entityId id;
		typeIdList types;
	};
}