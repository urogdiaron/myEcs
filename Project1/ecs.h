#pragma once
#include "archetype.h"

namespace ecs
{
	struct Ecs
	{
	public:
		Ecs();
		Ecs(const Ecs & src) = delete;

	private:
		template<typename...>
		friend struct View;
		friend struct Archetype;
		
		template<typename...>
		friend struct EntityCommand_Create;

		template<size_t ComponentCount>
		struct QueriedChunk
		{
			int entityCount;	// this exists in the chunk but for efficiency we copy it out here
			std::array<uint8_t*, ComponentCount + 1> buffers;
			Chunk* chunk;
		};

		template<class ...Ts>
		std::vector<QueriedChunk<sizeof...(Ts)>> get(const typeQueryList& typeIds)
		{
			std::vector<QueriedChunk<sizeof...(Ts)>> ret;
			ret.reserve(10);

			if constexpr (sizeof...(Ts) > 0)
			{
				typeId typeIdsToGet[] = { getTypeId<Ts>()... };
				for (auto& archetype : archetypes_)
				{
					if (archetype->hasAllComponents(typeIds))
					{
						for (auto& chunk : archetype->chunks)
						{
							if (chunk->size == 0)
								continue;

							auto& queriedChunk = ret.emplace_back();
							queriedChunk.chunk = chunk.get();
							queriedChunk.entityCount = queriedChunk.chunk->size;
							queriedChunk.buffers[0] = &queriedChunk.chunk->buffer[0];	// the first buffer is the entity ids
							for (int i = 0; i < (int)sizeof...(Ts); i++)
							{
								queriedChunk.buffers[i + 1] = queriedChunk.chunk->getArray(typeIdsToGet[i])->buffer;
							}
						}
					}
				}
			}
			else
			{
				for (auto& archetype : archetypes_)
				{
					if (archetype->hasAllComponents(typeIds))
					{
						for (auto& chunk : archetype->chunks)
						{
							if (chunk->size == 0)
								continue;

							auto& queriedChunk = ret.emplace_back();
							queriedChunk.chunk = chunk.get();
							queriedChunk.entityCount = queriedChunk.chunk->size;
							queriedChunk.buffers[0] = &queriedChunk.chunk->buffer[0];	// the first buffer is the entity ids
						}
					}
				}
			}
			return ret;
		}

		std::tuple<int, Archetype*> createArchetype(const typeIdList& typeIds);

		entityId createEntity_impl(const typeIdList& typeIds);

		template<class T>
		typeId getTypeId_impl() const
		{
			static size_t typeIndex = typeDescriptors_.size();
			if (typeDescriptors_.size() > typeIndex)
				return typeDescriptors_[typeIndex].get();
			return nullptr;
		}

		template<class T>
		typeId getTypeId() const
		{
			return getTypeId_impl<std::decay_t<T>>();
		}

		template<class... Ts>
		const typeIdList& getTypeIds() const
		{
			static bool needsSort = true;
			static typeIdList ret = typeIdList((int)typeDescriptors_.size(), { getTypeId<Ts>()... });
			return ret;
		}
	public:

		template<class T>
		void registerType(const char* name, ComponentType componentType = ComponentType::Regular)
		{
			typeId oldTypeId = getTypeId<T>();
			if (oldTypeId)
			{
				printf("Component \"%s\" is already registered!\n", name);
				return;
			}

			auto& typeDesc = typeDescriptors_.emplace_back(std::make_unique<TypeDescriptor>());
			typeDesc->index = (int)typeDescriptors_.size() - 1;
			typeDesc->size = sizeof(T);
			typeDesc->alignment = alignof(T);
			typeDesc->type = componentType;
			typeDesc->name = name;
			componentArrayFactory_.addFactoryFunction<T>(typeDesc.get());
			typeIds_.push_back(typeDesc.get());
		}

		template<class T>
		void setComponent(entityId id, const T& value)
		{
			T* comp = getComponent<T>(id);
			if (comp)
				*comp = value;
		}

		template<class T>
		void setSharedComponent(entityId id, const T& value)
		{
			entityDataIndex entityIndex = entityDataIndexMap_[id];
			Archetype* archetype = archetypes_[entityIndex.archetypeIndex].get();
			entityIndex = archetype->setSharedComponent(id, entityIndex, value);
			entityDataIndexMap_[id] = entityIndex;
		}

		template<class ...Ts>
		entityId createEntity(const Prefab<Ts...>& prefab)
		{
			return std::apply([&](auto& ...x) { return createEntity<Ts...>(x...); }, prefab.defaultValues);
		}

		template<class ...Ts, class ...Us>
		entityId createEntity(const Prefab<Ts...>& prefab, const Us&... initialValue)
		{
			auto prefabCopy = prefab;
			prefabCopy.setDefaultValue(initialValue...);
			return createEntity(prefabCopy);
		}

		template<class ...Ts>
		entityId createEntity()
		{
			entityId ret = createEntity_impl(getTypeIds<Ts...>());
			return ret;
		}

		template<class ...Ts>
		entityId createEntity(const Ts&... initialValue)
		{
			entityId ret = createEntity_impl(getTypeIds<Ts...>());
			int tmp[] = {(setComponent(ret, initialValue), 0)...};
			return ret;
		}

		// If you call this from the outside, keepStateComponents needs to be true. False is only for internal usage.
		bool deleteEntity(entityId id, bool keepStateComponents = true);

		template<class T>
		void addComponent(entityId id, const T& data)
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return;

			Archetype* oldArchetype = archetypes_[it->second.archetypeIndex].get();
			typeIdList newTypes = oldArchetype->containedTypes_;
			newTypes.addTypes({ getTypeId<T>() });
			changeComponents(id, newTypes);
			setComponent(id, data);
		}

		void deleteComponents(entityId id, const typeIdList& typeIds);

		void changeComponents(entityId id, const typeIdList& typeIds);

		template<class T>
		bool hasComponent(entityId id) const
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return false;

			typeQueryList queryList(typeDescriptors_.size());
			queryList.add(getTypeIds<T>(), TypeQueryItem::Mode::Read);
			return archetypes_[it->second.archetypeIndex]->hasAllComponents(queryList);
		}

		template<class T>
		T* getComponent(entityId id) const
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return nullptr;

			entityDataIndex entityIndex = it->second;
			Archetype* archetype = archetypes_[entityIndex.archetypeIndex].get();
			Chunk* chunk = archetype->chunks[entityIndex.chunkIndex].get();
			ComponentArrayBase* componentArray = chunk->getArray(getTypeId<T>());
			if (!componentArray)
				return nullptr;

			return static_cast<ComponentArray<T>*>(componentArray)->getElement(it->second.elementIndex);
		}

		typeId getTypeIdByName(const std::string& typeName);

private:
	private:
		template<class T>
		void getComponents_impl(Chunk* chunk, int elementIndex, T*& out)
		{
			auto componentArray = chunk->getArray(getTypeId<T>());
			if (!componentArray)
			{
				out = nullptr;
				return;
			}

			out = static_cast<ComponentArray<T>*>(componentArray)->getElement(elementIndex);
		}

		template<class T, class... Ts>
		void getComponents_impl(Chunk* chunk, int elementIndex, T*& out, Ts*&... restOut)
		{
			getComponents_impl(chunk, elementIndex, out);
			getComponents_impl(chunk, elementIndex, restOut...);
		}

public:
		template<class... Ts>
		std::tuple<Ts*...> getComponents(entityId id)
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return std::tuple<Ts *...>{};

			Archetype* archetype = archetypes_[it->second.archetypeIndex].get();
			Chunk* chunk = archetype->chunks[it->second.chunkIndex].get();

			std::tuple<Ts*...> ret = {};
			std::apply([&](auto& ...x) { getComponents_impl(chunk, it->second.elementIndex, x...); }, ret);
			return ret;
		}

		void save(std::ostream& stream) const;
		void load(std::istream& stream);

		ComponentArrayFactory componentArrayFactory_;
		std::vector<std::unique_ptr<TypeDescriptor>> typeDescriptors_;	// we store pointers so the raw TypeDescriptor* will stay stable for sure
		std::vector<typeId> typeIds_;	// This is the same as the typedescriptors but has no ownership. I didn't want the api to have unique_ptr all over the place
		std::unordered_map<entityId, entityDataIndex> entityDataIndexMap_;
		std::vector<std::unique_ptr<Archetype>> archetypes_;
		std::vector<std::unique_ptr<struct EntityCommand>> entityCommandBuffer_;
		std::unordered_map<entityId, entityId> temporaryEntityIdRemapping_;		// for EntityCommand_Create
		entityId nextEntityId = 1;
	};
}
