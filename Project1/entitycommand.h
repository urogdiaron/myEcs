#pragma once
#include "ecs.h"

namespace ecs
{
	struct EntityCommand
	{
		virtual void execute(struct Ecs& ecs) = 0;
	};

	struct EntityCommand_Create : EntityCommand
	{
		EntityCommand_Create(entityId id, const std::vector<typeId>& types)
			: temporaryId(id)
			, types(types)
		{}

		void execute(Ecs& ecs) override
		{
			entityId newId = ecs.createEntity(types);
			ecs.temporaryEntityIdRemapping_[temporaryId] = newId;
		}

		entityId temporaryId;
		std::vector<typeId> types;
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
				printf("EntityCommand_SetComponent: Component data not found. Id: %d; Type: %s.", id, ecs.getNameByTypeId(type_id<T>()));
		}

		entityId id;
		T data;
	};

	struct EntityCommand_ChangeComponents : EntityCommand
	{
		EntityCommand_ChangeComponents(entityId id, const std::vector<typeId>& types)
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
		std::vector<typeId> types;
	};
}