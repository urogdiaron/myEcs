#pragma once
#include "ecs_util.h"
#include <functional>

namespace ecs
{
	struct ComponentArrayBase
	{
		ComponentArrayBase(typeId tid, uint8_t* buffer) : tid(tid), buffer(buffer), elementSize(tid->size) {}
		virtual ~ComponentArrayBase() {}
		virtual void createEntity(int elementIndex) = 0;
		virtual void deleteEntity(int elementIndex, int lastValidElementIndex) = 0;
		virtual void copyFromArray(int sourceElementIndex, const ComponentArrayBase* sourceArray, int destElementIndex) = 0;
		virtual void moveFromArray(int sourceElementIndex, const ComponentArrayBase* sourceArray, int destElementIndex) = 0;
		virtual void save(std::ostream& stream, size_t count) const = 0;
		virtual void load(std::istream& stream, size_t count) = 0;
		typeId getTypeId() { return tid; }

		virtual bool isSameAsSharedComponent(const ComponentArrayBase* other) const = 0;

		ComponentData getElementData(int elementIndex)
		{
			ComponentData ret;
			ret.tid = tid;
			ret.data = buffer + elementSize * elementIndex;
			return ret;
		}

		virtual void saveElement(std::ostream& stream, int elementIndex) const = 0;
		virtual void loadElement(std::istream& stream, int elementIndex) = 0;

		uint8_t* buffer;
		int elementSize;
		typeId tid;
	};

	template<class T>
	struct ComponentArray : public ComponentArrayBase
	{
		ComponentArray(typeId tid, uint8_t* buffer) : ComponentArrayBase(tid, buffer) {}

		void createEntity(int elementIndex) override
		{
			new (&buffer[elementIndex * elementSize]) T{};
		}

		void deleteEntity(int elementIndex, int lastValidElementIndex) override
		{
			auto tBuffer = reinterpret_cast<T*>(buffer);
			std::swap(tBuffer[elementIndex], tBuffer[lastValidElementIndex]);
			tBuffer[lastValidElementIndex].~T();
		}

		void moveFromArray(int sourceElementIndex, const ComponentArrayBase* sourceArray, int destElementIndex) override
		{
			auto sourceArrayCasted = static_cast<const ComponentArray<T>*>(sourceArray);
			auto tSourceBuffer = reinterpret_cast<T*>(sourceArrayCasted->buffer);
			auto tDestBuffer = reinterpret_cast<T*>(buffer);

			if constexpr (!std::is_trivially_move_assignable_v<T>)
			{
				new (&buffer[destElementIndex * elementSize]) T{};
			}

			tDestBuffer[destElementIndex] = std::move(tSourceBuffer[sourceElementIndex]);
		}

		void copyFromArray(int sourceElementIndex, const ComponentArrayBase* sourceArray, int destElementIndex) override
		{
			auto sourceArrayCasted = static_cast<const ComponentArray<T>*>(sourceArray);
			auto tSourceBuffer = reinterpret_cast<T*>(sourceArrayCasted->buffer);
			auto tDestBuffer = reinterpret_cast<T*>(buffer);
			tDestBuffer[destElementIndex] = tSourceBuffer[sourceElementIndex];
		}

		T* getElement(int elementIndex)
		{
			auto tBuffer = reinterpret_cast<T*>(buffer);
			return &tBuffer[elementIndex];
		}

		const T* getElement(int elementIndex) const
		{
			auto tBuffer = reinterpret_cast<const T*>(buffer);
			return &tBuffer[elementIndex];
		}

		bool isElementEqualStreamed(const uint8_t* data, int elementIndex) const
		{
			return memcmp(data, getElement(elementIndex), elementSize) == 0;
		}

		bool isSameAsSharedComponent(const ComponentArrayBase* other) const override
		{
			if (other->tid != tid)
				return false;

			if (tid->type != ComponentType::Shared)
			{
				_ASSERT(0);
				return false;
			}

			return equals(*getElement(0), *static_cast<const ComponentArray<T>*>(other)->getElement(0));
		}

		void save(std::ostream& stream, size_t entityCount) const override
		{
			if constexpr (std::is_trivially_copyable_v<T>)
			{
				stream.write((char*)buffer, entityCount * sizeof(T));
			}
			/*else
			{
				for (int i = 0; i < entityCount; i++)
				{
					getElement(i)->save(stream);
				}
			}*/
		}

		void saveElement(std::ostream& stream, int elementIndex) const override
		{
			if constexpr (std::is_trivially_copyable_v<T>)
			{
				stream.write((char*)(buffer + elementIndex * elementSize), elementSize);
			}
		}

		void load(std::istream& stream, size_t count) override
		{
			if constexpr (std::is_trivially_copyable_v<T>)
			{
				stream.read((char*)buffer, count * sizeof(T));
			}
			/*else
			{
				for(int i = 0; i < count; i++)
				{
					getElement(i)->load(stream);
				}
			}*/
		}

		void loadElement(std::istream& stream, int elementIndex) override
		{
			if constexpr (std::is_trivially_copyable_v<T>)
			{
				stream.read((char*)(buffer + elementIndex * elementSize), elementSize);
			}
		}
	};

	struct ComponentArrayFactory
	{
		std::unique_ptr<ComponentArrayBase> create(const typeId& componentId, uint8_t* buffer) const
		{
			auto it = factoryFunctions.find(componentId);
			if (it == factoryFunctions.end())
				return nullptr;
			return it->second(buffer);
		}

		template<class T>
		void addFactoryFunction(const typeId& componentId)
		{
			std::function<std::unique_ptr<ComponentArrayBase>(uint8_t*)> fn;
			fn = [componentId](uint8_t* buffer)
			{
				return std::make_unique<ComponentArray<T>>(componentId, buffer);
			};

			factoryFunctions[componentId] = fn;
		}

		std::unordered_map<typeId, std::function<std::unique_ptr<ComponentArrayBase>(uint8_t*)>> factoryFunctions;
	};

	struct Chunk
	{
		Chunk()
		{
			entityCapacity = 0;
		}

		Chunk(struct Archetype* archetype, const std::vector<typeId>& typeIds, const ComponentArrayFactory& componentArrayFactory)
			: archetype(archetype)
		{
			int maxAlign = (int)alignof(std::max_align_t);
			int worstCaseCapacity = bufferCapacity - maxAlign * (int)typeIds.size();

			int entitySize = sizeof(entityId);
			int componentArrayCount = 0;
			int sharedComponentCount = 0;
			for (auto& t : typeIds)
			{
				if (t->type != ComponentType::Shared)
				{
					entitySize += t->size;
					componentArrayCount++;
				}
				else
				{
					worstCaseCapacity -= t->size;	// we need to store one of the shared components at the end of our buffer
					sharedComponentCount++;
				}
			}

			componentArrays.reserve(componentArrayCount);
			sharedComponents.reserve(sharedComponentCount);

			entityCapacity = worstCaseCapacity / entitySize;
			int componentBufferOffset = 0;
			
			// in the beginning there are entity ids
			componentBufferOffset += sizeof(entityId) * entityCapacity;

			for (auto& t : typeIds)
			{
				if (t->size == 0)
					continue;

				// align the offset to this type
				int under = componentBufferOffset % t->alignment;
				componentBufferOffset += ((t->alignment - under) % t->alignment);


				if (t->type == ComponentType::Shared)
				{
					auto& sharedComponentArray = sharedComponents.emplace_back(componentArrayFactory.create(t, &buffer[0] + componentBufferOffset));
					sharedComponentArray->createEntity(0);
					componentBufferOffset += t->size * 1;
				}
				else
				{
					componentArrays.emplace_back(componentArrayFactory.create(t, &buffer[0] + componentBufferOffset));
					componentBufferOffset += t->size * entityCapacity;
				}
			}
		}

		int createEntity(entityId id)
		{
			int entityIndex = size;
			getEntityIds()[entityIndex] = id;
			for (auto& componentArray : componentArrays)
			{
				componentArray->createEntity(entityIndex);
			}
			size++;
			return entityIndex;
		}

		// return value is the entity id that was moved to this position
		entityId deleteEntity(int elementIndex)
		{
			if (size == 0)
				return 0;

			// We need to do everything even if this is the last item to make sure we run the destructors in the componantArrays.
			size--;

			entityId* entityIds = getEntityIds();
			entityId movedEntityId = 0;
			if (size != elementIndex)
			{	// If we're not currently deleting the last item, we swap the last one here and return the swapped item to be updated
				movedEntityId = entityIds[size];
				entityIds[elementIndex] = movedEntityId;
			}

			for (auto& componentArray : componentArrays)
			{
				componentArray->deleteEntity(elementIndex, size);
			}

			return movedEntityId;
		}

		int moveEntityFromOtherChunk(Chunk* sourceChunk, int sourceElementIndex)
		{
			int ret = size;
			entityId* destEntityIds = getEntityIds();
			entityId* sourceEntityIds = sourceChunk->getEntityIds();
			destEntityIds[size] = sourceEntityIds[sourceElementIndex];

			for (int iDestType = 0; iDestType < (int)componentArrays.size(); iDestType++)
			{
				ComponentArrayBase* destArray = componentArrays[iDestType].get();
				ComponentArrayBase* sourceArray = sourceChunk->getArray(destArray->tid);
				if (sourceArray)
				{
					destArray->moveFromArray(sourceElementIndex, sourceArray, size);
				}
				else
				{
					destArray->createEntity(size);
				}
			}

			// Shared components should already be fine because the target chunk was selected (or created) with those in mind.
			
			size++;
			return ret;
		}

		entityId* getEntityIds()
		{
			return reinterpret_cast<entityId*>(&buffer[0]);
		}

		ComponentArrayBase* getArray(typeId tid)
		{
			for (auto& componentArray : componentArrays)
			{
				if (componentArray->tid == tid)
					return componentArray.get();
			}

			return nullptr;
		}

		ComponentArrayBase* getSharedComponentArray(typeId tid)
		{
			for (auto& componentArray : sharedComponents)
			{
				if (componentArray->tid == tid)
					return componentArray.get();
			}

			return nullptr;
		}

		template<class T>
		T* getSharedComponent(typeId tid)
		{
			for (auto& componentArray : sharedComponents)
			{
				if (componentArray->tid == tid)
					return static_cast<ComponentArray<T>*>(componentArray.get())->getElement(0);
			}

			return nullptr;
		}

		ComponentData getSharedComponentData(typeId tid)
		{
			ComponentData ret {tid};
			for (auto& componentArray : sharedComponents)
			{
				if (componentArray->tid == tid)
					ret = componentArray->getElementData(0);
			}

			return ret;
		}

		void save(std::ostream& stream) const
		{
			stream.write((char*)&size, sizeof(size));
			stream.write((char*)&buffer[0], size * sizeof(entityId));

			int lastIndex = -1;
			size_t componentTypeCount = componentArrays.size();
			for (int iComp = 0; iComp < componentTypeCount; iComp++)
			{
				auto componentArray = componentArrays[iComp].get();
				if (componentArray->tid->type == ComponentType::State)
					continue;
				int componentIndex = componentArray->tid->index;
				stream.write((char*)&componentIndex, sizeof(componentIndex));
				componentArray->save(stream, size);
			}
			stream.write((char*)&lastIndex, sizeof(lastIndex));

			size_t sharedComponentTypeCount = sharedComponents.size();
			for (int iComp = 0; iComp < sharedComponentTypeCount; iComp++)
			{
				auto componentArray = sharedComponents[iComp].get();
				if (componentArray->tid->type == ComponentType::State)
					continue;
				int componentIndex = componentArray->tid->index;
				stream.write((char*)&componentIndex, sizeof(componentIndex));
				componentArray->save(stream, 1);
			}
			stream.write((char*)&lastIndex, sizeof(lastIndex));
		}

		void load(std::istream& stream, const std::vector<typeId>& typeIdsByLoadedIndex)
		{
			stream.read((char*)&size, sizeof(size));
			stream.read((char*)getEntityIds(), size * sizeof(entityId));

			while(true)
			{
				int componentIndex;
				stream.read((char*)&componentIndex, sizeof(componentIndex));
				if (componentIndex < 0)
					break;

				typeId componentTypeId = typeIdsByLoadedIndex[componentIndex];
				auto componentArray = getArray(componentTypeId);
				componentArray->load(stream, size);
			}

			while(true)
			{
				int componentIndex;
				stream.read((char*)&componentIndex, sizeof(componentIndex));
				if (componentIndex < 0)
					break;

				typeId componentTypeId = typeIdsByLoadedIndex[componentIndex];
				auto componentArray = getSharedComponentArray(componentTypeId);
				componentArray->load(stream, 1);
			}
		}

		void saveElement(std::ostream& stream, int elementIndex) const
		{
			for (auto& componentArray : componentArrays)
			{
				stream.write((char*)&componentArray->tid->index, sizeof(componentArray->tid->index));
				componentArray->saveElement(stream, elementIndex);
			}
			int invalidIndex = -1;
			stream.write((char*)&invalidIndex, sizeof(invalidIndex));

		}

		void loadElement(std::istream& stream, const std::vector<typeId>& typeIdsByLoadedIndex, int elementIndex)
		{
			while (true)
			{
				int componentIndex;
				stream.read((char*)&componentIndex, sizeof(componentIndex));
				if (componentIndex < 0)
					break;

				typeId componentTypeId = typeIdsByLoadedIndex[componentIndex];
				auto componentArray = getArray(componentTypeId);
				componentArray->loadElement(stream, elementIndex);
			}
		}

		static inline const int bufferCapacity = 1 << 14;	// 16k chunks

		alignas(entityId)
		std::array<uint8_t, bufferCapacity> buffer;
		std::vector<std::unique_ptr<ComponentArrayBase>> componentArrays;
		std::vector<std::unique_ptr<ComponentArrayBase>> sharedComponents;

		int size = 0;
		int entityCapacity;

		struct Archetype* archetype;
	};
}