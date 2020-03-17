#pragma once

#include <unordered_map>
#include <memory>

namespace self_registering
{
	template<class TBase>
	struct Factory
	{
		virtual std::unique_ptr<TBase> create() = 0;
	};

	template<class TSpecial, class TBase>
	struct Factory_Spec : public Factory<TBase>
	{
		std::unique_ptr<TBase> create() override
		{
			return std::make_unique<TSpecial>();
		}
	};

	template<class TBase, class TKey>
	struct FactoryMap
	{
		static FactoryMap& getInstance()
		{
			static FactoryMap factoryMapSingleton;
			return factoryMapSingleton;
		}

		static std::unique_ptr<TBase> create(const TKey& typeName)
		{
			auto& factories = getInstance().factories_;
			auto it = factories.find(typeName);
			if (it != factories.end())
			{
				return it->second->create();
			}
			return nullptr;
		}

		std::unordered_map<TKey, std::unique_ptr<Factory<TBase>>> factories_;
	};

	template<class TSpecial, class TBase, class TKey>
	struct FactoryRegistrator
	{
		FactoryRegistrator()
		{
			auto& factoryMap = FactoryMap<TBase, TKey>::getInstance();
			factoryMap.factories_[TSpecial::getKey()] = std::make_unique<Factory_Spec<TSpecial, TBase>>();
		}
	};

	template<class TSpecial, class TBase, class TKey>
	struct SelfRegistering
	{
		static FactoryRegistrator<TSpecial, TBase, TKey> registrator_;
		static void init() {}
		virtual void forceInitialization() { registrator_; }
	};

	template<class TSpecial, class TBase, class TKey>
	FactoryRegistrator<TSpecial, TBase, TKey> SelfRegistering<TSpecial, TBase, TKey>::registrator_ = {};
}
