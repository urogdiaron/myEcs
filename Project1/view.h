#pragma once
#include "ecs_util.h"
#include "component_array.h"
#include "archetype.h"
#include "ecs.h"
#include "archetype_impl.h"
#include "ecs_impl.h"
#include "entitycommand.h"

namespace ecs
{
	template<class ...Ts>
	struct View
	{
		friend struct iterator;
		friend struct Ecs;

	private:
		View(Ecs& ecs)
			: ecs_(&ecs)
			, typeQueryList((int)ecs.typeDescriptors_.size())
		{
			const typeIdList& readTypeIds = ecs.getTypeIds_FilterConst<Ts...>(true);
			const typeIdList& writeTypeIds = ecs.getTypeIds_FilterConst<Ts...>(false);
			typeQueryList.add(readTypeIds, TypeQueryItem::Mode::Read);
			typeQueryList.add(writeTypeIds, TypeQueryItem::Mode::Write);
		}

	public:
		~View()
		{
		}

		View(const View& v) = default;
		View(View&& v) = default;

		template <class ...Cs>
		View with()
		{
			typeQueryList.add(ecs_->getTypeIds<Cs...>(), TypeQueryItem::Mode::Required);
			return std::move(*this);
		}

		template <class ...Cs>
		View exclude()
		{
			typeQueryList.add(ecs_->getTypeIds<Cs...>(), TypeQueryItem::Mode::Exclude);
			return std::move(*this);
		}

		template <class C>
		View filterShared(const C& sharedComponent)
		{
			initializeData();
			for (auto it = queriedChunks_.begin(); it != queriedChunks_.end();)
			{
				Chunk* chunk = it->chunk;
				const C* sharedValue = chunk->getSharedComponent<C>(ecs_->getTypeId<C>());
				if (!sharedValue || !equals(*sharedValue, sharedComponent))
					it = queriedChunks_.erase(it);
				else
					++it;
			}
			return std::move(*this);
		}

		bool lockUsedTypes()
		{
			bool locksAreOk = true;
			for (auto t : typeQueryList.read.calcTypeIds(ecs_->typeIds_))
			{
				locksAreOk = locksAreOk && ecs_->lockTypeForRead(t);
			}

			for (auto t : typeQueryList.write.calcTypeIds(ecs_->typeIds_))
			{
				locksAreOk = locksAreOk && ecs_->lockTypeForWrite(t);
			}
		}

		void unlockUsedTypes()
		{
			for (auto t : typeQueryList.read.calcTypeIds(ecs_->typeIds_))
			{
				ecs_->releaseTypeForRead(t);
			}

			for (auto t : typeQueryList.write.calcTypeIds(ecs_->typeIds_))
			{
				ecs_->releaseTypeForWrite(t);
			}
		}

		void initializeData()
		{
			if (!initialized_)
			{
				EASY_BLOCK("View Init");
				queriedChunks_ = ecs_->get<Ts...>(typeQueryList);
				initialized_ = true;
			}
		}

		template<bool TOnlyCurrentChunk = false>
		struct iterator
		{
			iterator() = default;
			iterator(View* v) : view(v)
			{
				view->initializeData();
				if (view->queriedChunks_.size() > 0)
				{
					chunkIndex = 0;
					entityIndex = 0;

					//createCurrentTuple(std::index_sequence_for<Ts...>());
				}
			}

			View* getView() const { return view; }

			iterator& operator++()
			{
				if (view->queriedChunks_[chunkIndex].entityCount - 1 > entityIndex)
				{
					entityIndex++;
				}
				else
				{
					if constexpr (TOnlyCurrentChunk)
					{
						chunkIndex = -1;
					}
					else
					{
						if ((int)view->queriedChunks_.size() - 1 > chunkIndex)
						{
							chunkIndex++;
							entityIndex = 0;
						}
						else
						{
							chunkIndex = -1;
						}
					}
				}

				//if (isValid())
				//{
				//	createCurrentTuple(std::index_sequence_for<Ts...>());
				//}
				return *this;
			}

			bool operator==(const iterator& rhs) const
			{
				if (!isValid() && !rhs.isValid())
					return true;

				return (chunkIndex == rhs.chunkIndex && entityIndex == rhs.entityIndex);
			}

			bool operator!=(const iterator& rhs) const
			{
				return !(*this == rhs);
			}

			bool isValid() const
			{
				return chunkIndex >= 0 && entityIndex >= 0;
			}

			template<class ...Ts>
			bool hasComponents() const
			{
				return view->queriedChunks_[chunkIndex].chunk->archetype->containedTypes_.hasAllTypes(view->ecs_->getTypeIds<Ts...>());
			}

			template<class TSharedComp>
			const TSharedComp* getSharedComponent() const
			{
				return view->queriedChunks_[chunkIndex].chunk->getSharedComponent<TSharedComp>(view->ecs_->getTypeId<TSharedComp>());
			}

			std::tuple<const entityId&, Ts&...> operator*() const
			{
				return createCurrentTuple(std::index_sequence_for<Ts...>());
				//return *reinterpret_cast<const std::tuple<const entityId&, Ts&...>*>(&currentTupleBuffer[0]);
			}

			template<class T>
			T& get() const { return std::get<T>(*this); }

			template<size_t... Is>
			std::tuple<const entityId&, Ts&...> createCurrentTuple(std::index_sequence<Is...>) const
			{
				return
				{
					reinterpret_cast<const entityId*>(view->queriedChunks_[chunkIndex].buffers[0])[entityIndex],
					reinterpret_cast<Ts*>(view->queriedChunks_[chunkIndex].buffers[Is + 1])[entityIndex]...
				};
			}

			template<class T>
			T* getComponent() const
			{
				Chunk* chunk = view->queriedChunks_[chunkIndex].chunk;
				auto tid = view->ecs_->getTypeId<T>();
				auto componentArray = static_cast<ComponentArray<std::decay_t<T>>*>(chunk->getArray(tid));
				if (!componentArray)
					return nullptr;
				return componentArray->getElement(entityIndex);
			}

			View* view = nullptr;
			//std::array<uint8_t, sizeof(std::tuple<const entityId&, Ts&...>)> currentTupleBuffer = {};
			int chunkIndex = -1;
			int entityIndex = -1;
		};

		iterator<false> begin() {
			return iterator<false>(this);
		}

		iterator<false> end()
		{
			return iterator<false>();
		}

		iterator<true> beginForChunk(int chunkIndex) {
			auto it = iterator<true>(this);
			it.chunkIndex = chunkIndex;
			return it;
		}

		iterator<true> endForChunk() {
			return iterator<true>();
		}

		template<class ...Ts, class ...Us>
		entityId createEntity(const Prefab<Ts...>& prefab, const Us&... initialValues)
		{
			entityId newId = -ecs_->getTempEntityId();
			ecs_->addToCommandBuffer(std::make_unique<EntityCommand_CreateFromPrefab<Prefab<Ts...>, Us...>>(newId, &prefab, initialValues...));
			return newId;
		}

		template<class ...Ts>
		entityId createEntity(const Ts... initialValues)
		{
			entityId newId = -ecs_->getTempEntityId();
			ecs_->addToCommandBuffer(std::make_unique<EntityCommand_Create<Ts...>>(newId, initialValues...));
			return newId;
		}

		void deleteEntity(entityId id)
		{
			ecs_->addToCommandBuffer(std::make_unique<EntityCommand_Delete>(id));
		}

		template<class... Ts>
		void deleteComponents(entityId id)
		{
			ecs_->addToCommandBuffer(std::make_unique<EntityCommand_DeleteComponents>(id, ecs_->getTypeIds<Ts...>()));
		}

		template<class T>
		void addComponent(entityId id, const T& data = T{})
		{
			ecs_->addToCommandBuffer(std::make_unique<EntityCommand_AddComponent<T>>(id, data));
		}

		template<class... Ts>
		void changeComponents(entityId id)
		{
			ecs_->addToCommandBuffer(std::make_unique<EntityCommand_ChangeComponents>(id, ecs_->getTypeIds<Ts...>()));
		}

		template<class T>
		void setComponentData(entityId id, const T& data)
		{
			ecs_->addToCommandBuffer(std::make_unique<EntityCommand_SetComponent<T>>(id, data));
		}

		template<class T>
		void setSharedComponentData(entityId id, const T& data)
		{
			ecs_->addToCommandBuffer(std::make_unique<EntityCommand_SetSharedComponent<T>>(id, data));
		}

		size_t getCount()
		{
			initializeData();
			size_t count = 0;
			for (auto& chunk : queriedChunks_)
			{
				count += chunk.entityCount;
			}

			return count;
		}

		Ecs* ecs_;
		typeQueryList typeQueryList;
		std::vector<Ecs::QueriedChunk<sizeof...(Ts)>> queriedChunks_;
		bool initialized_ = false;
	};
}