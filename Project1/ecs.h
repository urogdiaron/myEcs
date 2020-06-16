#pragma once
#include "archetype.h"
#include <atomic>
#include <mutex>

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

		template<typename>
		friend struct EntityCommand_SetComponent;

		template<typename>
		friend struct EntityCommand_SetSharedComponent;

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
					if (archetype && archetype->hasAllComponents(typeIds))
					{
						for (auto& chunk : archetype->chunks)
						{
							if (!chunk || chunk->size == 0)
								continue;

							auto& queriedChunk = ret.emplace_back();
							queriedChunk.chunk = chunk.get();
							queriedChunk.entityCount = queriedChunk.chunk->size;
							queriedChunk.buffers[0] = &queriedChunk.chunk->buffer[0];	// the first buffer is the entity ids
							for (int i = 0; i < (int)sizeof...(Ts); i++)
							{
								_ASSERT_EXPR(typeIdsToGet[i]->type != ComponentType::Shared, L"Use getSharedComponent on the iterator if you want to read a shared component!");
								_ASSERT_EXPR(typeIdsToGet[i]->size != 0, L"Attempting to read an empty class component! Use the with function on the View.");
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
					if (archetype && archetype->hasAllComponents(typeIds))
					{
						for (auto& chunk : archetype->chunks)
						{
							if (!chunk || chunk->size == 0)
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

	public:
		template<class... Ts>
		const typeIdList getTypeIds() const
		{
			typeIdList ret = typeIdList((int)typeDescriptors_.size(), { getTypeId<Ts>()... });
			return ret;
		}

		template<class... Ts>
		const typeIdList getTypeIds_FilterConst(bool keepConst) const
		{
			if(keepConst)
				return typeIdList((int)typeDescriptors_.size(), { getTypeId<Ts>()... }, { std::is_const<Ts>()... });
			else
				return typeIdList((int)typeDescriptors_.size(), { getTypeId<Ts>()... }, { !std::is_const<Ts>()... });
		}

		template<class... Ts>
		View<Ts...> view()
		{
			return View<Ts...>(*this);
		}

		void deleteArchetype(int archetypeIndex);
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
			if constexpr (std::is_empty_v<T>)
			{
				typeDesc->size = 0;
			}
			else
			{
				typeDesc->size = sizeof(T);
			}
			typeDesc->alignment = alignof(T);
			typeDesc->type = componentType;
			typeDesc->name = name;
			componentArrayFactory_.addFactoryFunction<T>(typeDesc.get());
			typeIds_.push_back(typeDesc.get());
		}

		template<class T>
		void setComponent(entityId id, const T& value)
		{
			typeId componentTypeId = getTypeId<T>();
			if (componentTypeId->size == 0)
				return;

			if (componentTypeId->type != ComponentType::Shared)
			{
				T* comp = getComponent<T>(id);
				if (comp)
					*comp = value;
			}
			else
			{
				setSharedComponent(id, value);
			}
		}

		template<class T>
		void setSharedComponent(entityId id, const T& value)
		{
			entityDataIndex oldEntityIndex = entityDataIndexMap_[id];
			Archetype* archetype = archetypes_[oldEntityIndex.archetypeIndex].get();
			auto [newEntityIndex, movedId] = archetype->setSharedComponent(oldEntityIndex, value);
			entityDataIndexMap_[id] = newEntityIndex;
			if (movedId)
				setEntityIndexMap(movedId, oldEntityIndex);
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

		template<class... Ts>
		bool hasAllComponents(entityId id) const
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return false;

			typeQueryList queryList(typeDescriptors_.size());
			queryList.add(getTypeIds<Ts...>(), TypeQueryItem::Mode::Read);
			return archetypes_[it->second.archetypeIndex]->hasAllComponents(queryList);
		}

		template<class... Ts>
		std::array<bool, sizeof...(Ts)> hasEachComponent(entityId id) const
		{
			auto it = entityDataIndexMap_.find(id);
			if (it == entityDataIndexMap_.end())
				return {};

			const Archetype* archetype = archetypes_[it->second.archetypeIndex].get();
			std::array<bool, sizeof...(Ts)> ret = { archetype->containedTypes_.hasType(getTypeId<Ts>())... };
			return ret;
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
			typeId componentTypeId = getTypeId<T>();
			_ASSERT_EXPR(componentTypeId->type != ComponentType::Shared, L"Use the chunk's getSharedComponent for shared components!");
			_ASSERT_EXPR(componentTypeId->size != 0, L"Can't use getComponent on empty class components. Use hasComponent instead to check for existence.");
			ComponentArrayBase* componentArray = chunk->getArray(componentTypeId);
			if (!componentArray)
				return nullptr;

			return static_cast<ComponentArray<T>*>(componentArray)->getElement(it->second.elementIndex);
		}

		typeId getTypeIdByName(const std::string& typeName);

		void executeCommmandBuffer();

private:
	entityId getTempEntityId()
	{
		entityId ret = nextTempEntityId.fetch_add(1);
		return ret;
	}

	void addToCommandBuffer(std::unique_ptr<struct EntityCommand>&& command)
	{
		commandBufferMutex.lock();
		entityCommandBuffer_.emplace_back(std::move(command));
		commandBufferMutex.unlock();
	}

	template<class T>
	void getComponents_impl(Chunk* chunk, int elementIndex, T*& out)
	{
		typeId componentTypeId = getTypeId<T>();
		_ASSERT_EXPR(componentTypeId->size != 0, L"Can't use getComponent on empty class components. Use hasComponent instead to check for existence.");

		auto componentArray = chunk->getArray(componentTypeId);
		if (!componentArray)
		{
			out = nullptr;
			return;
		}

		out = static_cast<ComponentArray<std::decay_t<T>>*>(componentArray)->getElement(elementIndex);
	}

	template<class T, class... Ts>
	void getComponents_impl(Chunk* chunk, int elementIndex, T*& out, Ts*&... restOut)
	{
		getComponents_impl(chunk, elementIndex, out);
		getComponents_impl(chunk, elementIndex, restOut...);
	}

	bool setEntityIndexMap(entityId id, entityDataIndex index)
	{
		if (!id)
		{
			printf("setEntityIndexMap (%d) invalid id to index: %d, %d, %d\n", id, index.archetypeIndex, index.chunkIndex, index.elementIndex);
			return false;
		}

		if (index.archetypeIndex < 0 || index.archetypeIndex >= archetypes_.size() || !archetypes_[index.archetypeIndex])
		{
			printf("setEntityIndexMap (%d) invalid archetypeIndex: %d, %d, %d\n", id, index.archetypeIndex, index.chunkIndex, index.elementIndex);
			return false;
		}

		auto arch = archetypes_[index.archetypeIndex].get();
		if (index.chunkIndex < 0 || index.chunkIndex >= arch->chunks.size() || !arch->chunks[index.chunkIndex])
		{
			printf("setEntityIndexMap (%d) invalid chunkIndex: %d, %d, %d\n", id, index.archetypeIndex, index.chunkIndex, index.elementIndex);
			return false;
		}

		auto chunk = arch->chunks[index.chunkIndex].get();
		if (index.elementIndex < 0 || index.elementIndex >= chunk->size)
		{
			printf("setEntityIndexMap (%d) invalid elementIndex: %d, %d, %d\n", id, index.archetypeIndex, index.chunkIndex, index.elementIndex);
			return false;
		}

		entityId actualId = reinterpret_cast<entityId*>(&chunk->buffer[0])[index.elementIndex];
		if (actualId != id)
		{
			printf("setEntityIndexMap (%d) actual id at the place is %d: %d, %d, %d\n", id, actualId, index.archetypeIndex, index.chunkIndex, index.elementIndex);
			return false;
		}

		entityDataIndexMap_[id] = index;
		return true;
	}

public:
	bool lockTypeForRead(typeId t);
	bool lockTypeForWrite(typeId t);
	void releaseTypeForRead(typeId t);
	void releaseTypeForWrite(typeId t);

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

		void savePrefab(std::ostream& stream, entityId id) const;

		template<class... Ts>
		void savePrefab(std::ostream& stream, const Prefab<Ts...>& prefab);

		entityId createEntityFromPrefabStream(std::istream& stream);
		void save(std::ostream& stream) const;
		void load(std::istream& stream);

		ComponentArrayFactory componentArrayFactory_;
		std::vector<std::unique_ptr<TypeDescriptor>> typeDescriptors_;	// we store pointers so the raw TypeDescriptor* will stay stable for sure
		std::vector<typeId> typeIds_;	// This is the same as the typedescriptors but has no ownership. I didn't want the api to have unique_ptr all over the place
		std::unordered_map<entityId, entityDataIndex> entityDataIndexMap_;
		std::vector<std::unique_ptr<Archetype>> archetypes_;
		std::vector<std::unique_ptr<struct EntityCommand>> entityCommandBuffer_;
		std::unordered_map<entityId, entityId> temporaryEntityIdRemapping_;		// for EntityCommand_Create
		
		std::mutex commandBufferMutex;

		std::vector<typeId> lockedForRead;
		std::vector<typeId> lockedForWrite;

		entityId nextEntityId = 1;
		std::atomic<entityId> nextTempEntityId = 1;
	};
}
