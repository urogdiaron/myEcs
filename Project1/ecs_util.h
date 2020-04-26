#pragma once
#include <vector>
#include <string>
#include <utility>

namespace ecs
{
	template<class T>
	void makeVectorUniqueAndSorted(std::vector<T>& v)
	{
		std::sort(v.begin(), v.end());
		v.erase(std::unique(v.begin(), v.end()), v.end());
	}

	template<class T>
	void deleteFromVectorUnsorted(std::vector<T>& v, int index)
	{
		if ((int)v.size() - 1 > index)
		{
			std::swap(v[index], *(--v.end()));
		}
		v.pop_back();
	}

	using entityId = int;

	using typeIndex = int;
	struct TypeDescriptor
	{
		typeIndex index;
		std::string name;
	};

	using typeId = TypeDescriptor*;

	struct typeIdList
	{
		typeIdList(int totalTypeCount, std::initializer_list<typeId> tids)
			: typeIds(tids)
		{
			int byteCount = (totalTypeCount + 7) / 8;
			bitField.resize(byteCount);
			for (auto& tid : typeIds)
			{
				int byteIndex = tid->index / 8;
				int bitIndex = tid->index % 8;
				bitField[byteIndex] |= 1 << bitIndex;
			}

			makeVectorUniqueAndSorted(typeIds);
		}

		bool operator==(const typeIdList& rhs) const
		{
			return bitField == rhs.bitField;
		}

		void addTypes(const std::vector<typeId>& tids)
		{
			for (auto tid : tids)
			{
				typeIds.push_back(tid);
				setBit(tid->index);
			}

			makeVectorUniqueAndSorted(typeIds);
		}

		void deleteTypes(const std::vector<typeId>& tids)
		{
			for (auto t : tids)
			{
				auto it = std::find(typeIds.begin(), typeIds.end(), t);
				if (it != typeIds.end())
				{
					typeIds.erase(it);
					clearBit(t->index);
				}
			}
		}

		const std::vector<typeId>& getTypeIds() const { return typeIds; }
		const std::vector<uint8_t> getBitfield() const { return bitField; }

	private:
		void setBit(int typeIndex)
		{
			int byteIndex = typeIndex / 8;
			int bitIndex = typeIndex % 8;
			bitField[byteIndex] |= 1 << bitIndex;
		}

		void clearBit(int typeIndex)
		{
			int byteIndex = typeIndex / 8;
			int bitIndex = typeIndex % 8;
			bitField[byteIndex] &= ~(1 << bitIndex);
		}

		std::vector<uint8_t> bitField;
		std::vector<typeId> typeIds;
	};

	struct TypeQueryItem
	{
		enum Mode { Read, Write, Exclude, Required };
		typeId type;
		Mode mode;
	};

	struct typeQueryList
	{
		typeQueryList(int totalTypeCount)
			: required(totalTypeCount, {})
			, excluded(totalTypeCount, {})
		{
		}

		void add(const std::vector<typeId>& tids, TypeQueryItem::Mode mode)
		{
			for (auto tid : tids)
			{
				queryItems.push_back({ tid, mode });
			}

			if (mode != TypeQueryItem::Exclude)
			{
				required.addTypes(tids);
			}
			else
			{
				excluded.addTypes(tids);
			}
		}

		bool check(const typeIdList& listToCheck) const
		{
			for (size_t i = 0; i < listToCheck.getBitfield().size(); i++)
			{
				uint8_t byteToCheck = listToCheck.getBitfield()[i];
				uint8_t byteReq = required.getBitfield()[i];
				uint8_t byteEx = excluded.getBitfield()[i];
				if ((byteToCheck & byteReq) != byteReq)
					return false;

				if ((byteToCheck & byteEx) != 0)
					return false;
			}
			return true;
		}

	private:
		std::vector<TypeQueryItem> queryItems;

		typeIdList required;
		typeIdList excluded;
	};

	template<class... Ts>
	struct Prefab
	{
		Prefab() = default;

		template<class ...Us>
		Prefab(const Us&... defaultValues)
		{
			setDefaultValue(defaultValues...);
		}

		template<class T>
		void setDefaultValue(const T defaultValue)
		{
			static const bool typeIsInThePrefab = { (std::is_same_v<T, Ts> || ...) };
			static_assert(typeIsInThePrefab, "Prefab was initialized with default value of a type that is not in the prefab.");
			std::get<T>(defaultValues) = defaultValue;
		}

		template<class T, class ...Us>
		void setDefaultValue(T&& defaultValue, const Us&... defaultValues)
		{
			setDefaultValue(defaultValue);
			setDefaultValue(defaultValues...);
		}

		std::tuple<Ts...> defaultValues;
	};
}