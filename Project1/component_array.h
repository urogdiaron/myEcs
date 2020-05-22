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
		virtual void moveFromArray(int sourceElementIndex, const ComponentArrayBase* sourceArray, int destElementIndex) = 0;
		virtual void save(std::ostream& stream) const = 0;
		virtual void load(std::istream& stream, size_t count) = 0;
		typeId getTypeId() { return tid; }

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
			tDestBuffer[destElementIndex] = std::move(tSourceBuffer[sourceElementIndex]);
		}

		T* getElement(int elementIndex)
		{
			auto tBuffer = reinterpret_cast<T*>(buffer);
			return &tBuffer[elementIndex];
		}

		void save(std::ostream& stream) const override
		{
			//if constexpr (std::is_trivially_copyable_v<T>)
			//{
			//	stream.write((const char*)components_.data(), components_.size() * sizeof(T));
			//}
			/*else
			{
				for (auto& c : components_)
				{
					c.save(stream);
				}
			}*/
		}

		void load(std::istream& stream, size_t count) override
		{
			//components_.resize(count);
			//if constexpr (std::is_trivially_copyable_v<T>)
			//{
			//	stream.read((char*)components_.data(), count * sizeof(T));
			//}
			/*else
			{
				for (auto& c : components_)
				{
					c.load(stream);
				}
			}*/
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
			buffer = {};
			entityCapacity = 0;
		}

		Chunk(struct Archetype* archetype, const std::vector<typeId>& typeIds, const ComponentArrayFactory& componentArrayFactory)
			: archetype(archetype)
		{
			buffer = {};

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
			size--;
			if (size == 0)
				return 0;

			entityId* entityIds = getEntityIds();
			entityId movedEntityId = entityIds[size];
			entityIds[elementIndex] = movedEntityId;

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