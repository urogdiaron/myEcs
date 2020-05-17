#pragma once
#include "ecs.h"
#include "entitycommand.h"

namespace ecs
{
	template<class ...Ts>
	struct View
	{
		friend struct iterator;

		View(Ecs& ecs, bool autoExecuteCommandBuffer = true)
			: ecs_(&ecs)
			, autoExecuteCommandBuffer_(autoExecuteCommandBuffer)
			, typeQueryList((int)ecs.typeDescriptors_.size())
		{
			const typeIdList& exposedTypeIds = ecs.getTypeIds<Ts...>();
			typeQueryList.add(exposedTypeIds, TypeQueryItem::Mode::Write);
		}

		~View()
		{
			if (autoExecuteCommandBuffer_)
			{
				executeCommmandBuffer();
			}
		}

		View(const View&) = delete;
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

	private:
		void initializeData()
		{
			if (!data_)
				data_ = &ecs_->get<Ts...>(typeQueryList, archetypes_);
		}
	public:
		struct iterator
		{
			iterator() = default;
			iterator(View* v) : view(v)
			{
				view->initializeData();
				auto& entityIds = std::get<std::vector<std::vector<entityId>*>>(*view->data_);
				if (entityIds.size() > 0)
				{
					vectorIndex = 0;
					if (entityIds[vectorIndex]->size() > 0)
						elementIndex = 0;
				}
			}

			iterator& operator++()
			{
				auto& entityIds = std::get<std::vector<std::vector<entityId>*>>(*view->data_);
				if ((int)entityIds[vectorIndex]->size() - 1 > elementIndex)
				{
					elementIndex++;
				}
				else
				{
					if ((int)entityIds.size() - 1 > vectorIndex)
					{
						vectorIndex++;
						if (entityIds[vectorIndex]->size() > 0)
							elementIndex = 0;
						else
							elementIndex = -1;
					}
					else
					{
						vectorIndex = -1;
					}
				}
				return *this;
			}

			bool operator==(const iterator& rhs) const
			{
				if (!isValid() && !rhs.isValid())
					return true;

				return (vectorIndex == rhs.vectorIndex && elementIndex == rhs.elementIndex);
			}

			bool operator!=(const iterator& rhs) const
			{
				return !(*this == rhs);
			}

			bool isValid() const
			{
				return vectorIndex >= 0 && elementIndex >= 0;
			}

			entityId getId() const
			{
				auto& vectors = std::get<std::vector<std::vector<entityId>*>>(*view->data_);
				elementOut = (*vectors[vectorIndex])[elementIndex];
			}

			template<class ...Ts>
			bool hasComponents() const
			{
				return getCurrentArchetype()->containedTypes_.hasAllTypes(view->ecs_->getTypeIds<Ts...>());
			}

			std::tuple<const iterator&, const entityId&, Ts &...> operator*()
			{
				return getCurrentTuple();
			}

			template<class T>
			T& getCurrentItem()
			{
				auto& vectors = std::get<std::vector<std::vector<std::decay_t<T>>*>>(*view->data_);
				return (*vectors[vectorIndex])[elementIndex];
			}

			std::tuple<const iterator&, const entityId&, Ts &...> getCurrentTuple()
			{
				return { *this, getCurrentItem<const entityId>(), getCurrentItem<Ts>()... };
			}

			Archetype* getCurrentArchetype() const
			{
				return (*view->archetypes_)[vectorIndex];
			}

			View* view = nullptr;
			int vectorIndex = -1;
			int elementIndex = -1;
		};

		iterator begin() {
			return iterator(this);
		}

		iterator end()
		{
			return iterator();
		}

		template<class ...Ts, class ...Us>
		entityId createEntity(const Prefab<Ts...>& prefab, const Us&... initialValues)
		{
			entityId newId = -((int)ecs_->entityCommandBuffer_.size() + 1);
			ecs_->entityCommandBuffer_.push_back(std::make_unique<EntityCommand_CreateFromPrefab<Prefab<Ts...>, Us...>>(newId, &prefab, initialValues...));
			return newId;
		}

		template<class ...Ts>
		entityId createEntity(const Ts... initialValues)
		{
			entityId newId = -((int)ecs_->entityCommandBuffer_.size() + 1);
			ecs_->entityCommandBuffer_.push_back(std::make_unique<EntityCommand_Create<Ts...>>(newId, initialValues...));
			return newId;
		}

		void deleteEntity(entityId id)
		{
			ecs_->entityCommandBuffer_.push_back(std::make_unique<EntityCommand_Delete>(id));
		}

		template<class... Ts>
		void deleteComponents(entityId id)
		{
			ecs_->entityCommandBuffer_.push_back(std::make_unique<EntityCommand_DeleteComponents>(id, ecs_->getTypeIds<Ts...>()));
		}

		template<class T>
		void addComponent(entityId id, const T& data = T{})
		{
			ecs_->entityCommandBuffer_.push_back(std::make_unique<EntityCommand_AddComponent<T>>(id, data));
		}

		template<class... Ts>
		void changeComponents(entityId id)
		{
			ecs_->entityCommandBuffer_.push_back(std::make_unique<EntityCommand_ChangeComponents>(id, ecs_->getTypeIds<Ts...>()));
		}

		template<class T>
		void setComponentData(entityId id, const T& data)
		{
			ecs_->entityCommandBuffer_.push_back(std::make_unique<EntityCommand_SetComponent<T>>(id, data));
		}

		void executeCommmandBuffer()
		{
			if (ecs_->entityCommandBuffer_.size() == 0)
				return;

			for (auto& itCommand : ecs_->entityCommandBuffer_)
			{
				itCommand->execute(*ecs_);
			}

			ecs_->entityCommandBuffer_.clear();
			ecs_->temporaryEntityIdRemapping_.clear();
		}

		size_t getCount()
		{
			initializeData();
			size_t count = 0;
			const std::vector<std::vector<entityId>*>& entityIdArrays = std::get<0>(*data_);
			for (auto& v : entityIdArrays)
				count += v->size();

			return count;
		}

		Ecs* ecs_;
		typeQueryList typeQueryList;
		std::vector<Archetype*>* archetypes_ = nullptr;
		const std::tuple<std::vector<std::vector<entityId>*>, std::vector<std::vector<std::decay_t<Ts>>*>...>* data_ = nullptr;
		bool autoExecuteCommandBuffer_ = true;
	};

	template<class ...Ts>
	struct ViewBuilder
	{
		Ecs* ecs;
		
	};
}