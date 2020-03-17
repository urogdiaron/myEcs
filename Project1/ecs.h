#pragma once
#include "archetype.h"

namespace ecs
{
	struct Ecs
	{
		template<class T>
		void get_impl(const std::vector<typeId>& typeIds, std::vector<std::vector<T>*>& out)
		{
			for (auto& itArchetype : archetypes_)
			{
				if (!itArchetype->hasAllComponents(typeIds))
				{
					continue;
				}

				ComponentArrayBase* componentArray = itArchetype->get(type_id<T>());
				if (componentArray)
				{
					out.push_back(&static_cast<ComponentArray<T>*>(componentArray)->components_);
				}
			}
		}

		template<>
		void get_impl(const std::vector<typeId>& typeIds, std::vector<std::vector<entityId>*>& out)
		{
			for (auto& itArchetype : archetypes_)
			{
				if (!itArchetype->hasAllComponents(typeIds))
				{
					continue;
				}

				out.push_back(&itArchetype->entityIds_);
			}
		}

		template<class T, class ...Ts>
		void get_impl(const std::vector<typeId>& typeIds, std::vector<std::vector<T>*>& out, std::vector<std::vector<Ts>*>&... restOut)
		{
			get_impl(typeIds, out);
			get_impl(typeIds, restOut...);
		}

		template<class ...Ts>
		std::tuple<std::vector<std::vector<entityId>*>, std::vector<std::vector<Ts>*>...> get()
		{
			std::tuple<std::vector<std::vector<entityId>*>, std::vector<std::vector<Ts>*>...> ret = {};
			std::apply([&](auto& ...x) { get_impl(getTypes<Ts...>(), x...); }, ret);
			return ret;
		}

		std::tuple<int, Archetype*> createArchetype(const std::vector<typeId>& typeIds)
		{
			int i = 0;
			for (auto& itArchetype : archetypes_)
			{
				if (itArchetype->containedTypes_ == typeIds)
				{
					return { i, itArchetype.get() };
				}
				i++;
			}

			auto& insertedItem = archetypes_.emplace_back(std::make_unique<Archetype>(typeIds));
			return { i, insertedItem.get() };
		}

		entityId createEntity(const std::vector<typeId>& typeIds)
		{
			entityId newEntityId = nextEntityId++;
			auto [archIndex, archetype] = createArchetype(typeIds);
			int elementIndex = archetype->createEntity(newEntityId);
			entityDataIndexMap_[newEntityId] = { archIndex, elementIndex };
			return newEntityId;
		}

		bool deleteEntity(entityId id)
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return false;

			Archetype* arch = archetypes_[it->second.archetypeIndex].get();
			arch->deleteEntity(it->second.elementIndex);

			for (int i = it->second.elementIndex; i < arch->entityIds_.size(); i++)
			{
				entityId changedId = arch->entityIds_[i];
				auto changedIt = entityDataIndexMap_.find(changedId);
				if (changedIt != entityDataIndexMap_.end())
				{
					changedIt->second.elementIndex = i;
				}
			}

			return true;
		}

		template<class T>
		void registerType(const char* name)
		{
			ComponentArray<T>::getKey(); // Just to make sure that the selfRegistering code gets generated
			auto it = typeIdsByName_.find(name);
			if (it != typeIdsByName_.end())
				return;		// Multiple registration

			typeIdsByName_[name] = type_id<T>();
		}

		typeId getTypeIdByName(const char* name)
		{
			auto it = typeIdsByName_.find(name);
			if (it != typeIdsByName_.end())
				return it->second;

			return 0;
		}

		void changeComponents(entityId id, const std::vector<typeId>& typeIds)
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return;

			Archetype* oldArchetype = archetypes_[it->second.archetypeIndex].get();
			auto newTypes = typeIds;
			makeVectorUniqueAndSorted(newTypes);

			auto [archIndex, archetype] = createArchetype(newTypes);
			if (archetype == oldArchetype)
				return;

			int newElementIndex = archetype->copyFromEntity(id, it->second.elementIndex, oldArchetype);
			deleteEntity(id);
			entityDataIndexMap_[id] = { archIndex, newElementIndex };
		}

		template<class T>
		T* getComponent(entityId id) const
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return nullptr;

			auto itComponentArray = archetypes_[it->second.archetypeIndex]->componentArrays_.find(type_id<T>());
			if (itComponentArray == archetypes_[it->second.archetypeIndex]->componentArrays_.end())
			{
				return nullptr;
			}

			return &static_cast<ComponentArray<T>*>(itComponentArray->second.get())->components_[it->second.elementIndex];
		}

		void printArchetypes()
		{
			auto fnGetTypeNameById = [&](typeId id) -> const char*
			{
				for (auto& it : typeIdsByName_)
				{
					if (it.second == id)
						return it.first.c_str();
				}
				return "";
			};

			int i = 0;
			for (auto& arch : archetypes_)
			{
				printf("Archetype %0.2d\n", i);
				printf("Types: ");
				for (auto type : arch->containedTypes_)
				{
					printf("%s, ", fnGetTypeNameById(type));
				}
				printf("\n");

				printf("Entities: ");
				for (auto id : arch->entityIds_)
				{
					printf("%0.2d, ", id);
				}
				printf("\n\n");
				i++;
			}
		}

		struct entityDataIndex
		{
			int archetypeIndex;
			int elementIndex;
		};

		std::unordered_map<std::string, typeId> typeIdsByName_;
		std::unordered_map<entityId, entityDataIndex> entityDataIndexMap_;
		std::vector<std::unique_ptr<Archetype>> archetypes_;
		entityId nextEntityId = 1;
	};
}
