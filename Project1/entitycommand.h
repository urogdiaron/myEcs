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

	template<class TPrefab, class ...Us>
	struct EntityCommand_CreateFromPrefab : EntityCommand
	{
		EntityCommand_CreateFromPrefab(entityId id, const TPrefab* prefab, const Us&... initialValues)
			: temporaryId(id)
			, prefab(prefab)
		{
			args = std::make_tuple(initialValues...);
		}

		void execute(Ecs& ecs) override
		{
			entityId newId = std::apply([&](auto... x) { return ecs.createEntity(*prefab, x...); }, args);
			ecs.temporaryEntityIdRemapping_[temporaryId] = newId;
		}

		entityId temporaryId;
		const TPrefab* prefab;
		std::tuple<Us...> args;
	};

	struct EntityCommand_Delete : EntityCommand
	{
		EntityCommand_Delete(entityId id)
			: id(id)
		{}

		void execute(struct Ecs& ecs) override 
		{
			entityId idToDelete = id;
			if (idToDelete < 0)
			{
				idToDelete = ecs.temporaryEntityIdRemapping_[id];
			}

			ecs.deleteEntity(idToDelete, true);
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
			entityId idToUse = id;
			if (idToUse < 0)
				idToUse = ecs.temporaryEntityIdRemapping_[id];

			auto comp = ecs.getComponent<T>(idToUse);
			if (comp)
				*comp = data;
			else
				printf("EntityCommand_SetComponent: Component data not found. Id: %d; Type: %s.", idToUse, ecs.getTypeId<T>()->name.c_str());
		}

		entityId id;
		T data;
	};

	template<class T>
	struct EntityCommand_SetSharedComponent : EntityCommand
	{
		EntityCommand_SetSharedComponent(entityId id, const T& data)
			: id(id)
			, data(data)
		{}

		void execute(struct Ecs& ecs) override
		{
			entityId idToUse = id;
			if (idToUse < 0)
				idToUse = ecs.temporaryEntityIdRemapping_[id];

			ecs.setSharedComponent(idToUse, data);
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
			entityId idToUse = id;
			if (idToUse < 0)
				idToUse = ecs.temporaryEntityIdRemapping_[id];

			ecs.deleteComponents(idToUse, types);
		}

		entityId id;
		typeIdList types;
	};

	template<class T>
	struct EntityCommand_AddComponent : EntityCommand
	{
		EntityCommand_AddComponent(entityId id, const T& data)
			: id(id)
			, data(data)
		{}

		void execute(struct Ecs& ecs) override
		{
			entityId idToUse = id;
			if (idToUse < 0)
				idToUse = ecs.temporaryEntityIdRemapping_[id];

			ecs.addComponent(idToUse, data);
		}

		entityId id;
		T data;
	};

	struct EntityCommand_ChangeComponents : EntityCommand
	{
		EntityCommand_ChangeComponents(entityId id, const typeIdList& types)
			: id(id)
			, types(types)
		{}

		void execute(struct Ecs& ecs) override
		{
			entityId idToUse = id;
			if (idToUse < 0)
				idToUse = ecs.temporaryEntityIdRemapping_[id];

			ecs.changeComponents(idToUse, types);
		}

		entityId id;
		typeIdList types;
	};
}