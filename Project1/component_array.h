#pragma once
#include "ecs_util.h"
#include <functional>

namespace ecs
{
	struct ComponentArrayBase
	{
		ComponentArrayBase(typeId tid) : tid(tid) {}
		virtual ~ComponentArrayBase() {}
		virtual void createEntity() = 0;
		virtual void deleteEntity(int elementIndex) = 0;
		virtual void copyFromArray(int sourceElementIndex, const ComponentArrayBase* sourceArray) = 0;
		virtual void save(std::ostream& stream) const = 0;
		virtual void load(std::istream& stream) = 0;
		typeId getTypeId() { return tid; }
	protected:
		typeId tid;
	};

	template<class T>
	struct ComponentArray : public ComponentArrayBase
	{
		ComponentArray(typeId tid) : ComponentArrayBase(tid) {}

		void createEntity() override
		{
			components_.push_back(T{});
		}

		void deleteEntity(int elementIndex) override
		{
			deleteFromVectorUnsorted(components_, elementIndex);
		}

		void copyFromArray(int sourceElementIndex, const ComponentArrayBase* sourceArray) override
		{
			auto sourceArrayCasted = static_cast<const ComponentArray<T>*>(sourceArray);
			components_.push_back(sourceArrayCasted->components_[sourceElementIndex]);
		}

		void save(std::ostream& stream) const override
		{
			size_t count = components_.size();
			stream.write((const char*)&count, sizeof(count));
			if constexpr (std::is_trivially_copyable_v<T>)
			{
				stream.write((const char*)components_.data(), components_.size() * sizeof(T));
			}
			else
			{
				for (auto& c : components_)
				{
					c.save(stream);
				}
			}
		}

		void load(std::istream& stream) override
		{
			size_t count = 0;
			stream.read((char*)&count, sizeof(count));
			components_.resize(count);
			if constexpr (std::is_trivially_copyable_v<T>)
			{
				stream.read((char*)components_.data(), count * sizeof(T));
			}
			else
			{
				for (auto& c : components_)
				{
					c.load(stream);
				}
			}
		}

		std::vector<T> components_;
	};

	struct ComponentArrayFactory
	{
		std::unique_ptr<ComponentArrayBase> create(const typeId& componentId) const
		{
			auto it = factoryFunctions.find(componentId);
			if (it == factoryFunctions.end())
				return nullptr;
			return it->second();
		}

		template<class T>
		void addFactoryFunction(const typeId& componentId)
		{
			std::function<std::unique_ptr<ComponentArrayBase>()> fn;
			fn = [componentId]()
			{
				return std::make_unique<ComponentArray<T>>(componentId);
			};

			factoryFunctions[componentId] = fn;
		}

		std::unordered_map<typeId, std::function<std::unique_ptr<ComponentArrayBase>()>> factoryFunctions;
	};
}