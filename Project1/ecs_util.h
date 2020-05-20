#pragma once
#include <vector>
#include <array>
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

	template<class T>
	void saveVector(std::ostream& stream, const std::vector<T>& v)
	{
		size_t count = v.size();
		stream.write((const char*)&count, sizeof(count));
		stream.write((const char*)v.data(), count * sizeof(T));
	}

	template<class T>
	void loadVector(std::istream& stream, std::vector<T>& v)
	{
		size_t count = 0;
		stream.read((char*)&count, sizeof(count));
		v.resize(count);
		stream.read((char*)v.data(), count * sizeof(T));
	}

	enum class ComponentType
	{
		Regular,
		DontSave,
		State,		// Not saved, entity deletion keeps these alive
		Internal	// For internal use
	};

	using entityId = int;

	using typeIndex = int;
	struct TypeDescriptor
	{
		typeIndex index;
		int size;
		int alignment;
		ComponentType type;
		std::string name;
	};

	using typeId = TypeDescriptor*;


#define DEBUG_TYPEIDLISTS

	struct entityDataIndex
	{
		int archetypeIndex;
		int chunkIndex;
		int elementIndex;
	};

	struct typeIdList
	{
		typeIdList(size_t totalTypeCount, std::initializer_list<typeId> tids)
		{
			//size_t byteCount = (totalTypeCount + 7) / 8;
			//bitField.resize(byteCount);
			for (auto& tid : tids)
			{
				size_t byteIndex = tid->index / 8;
				size_t bitIndex = tid->index % 8;
				bitField[byteIndex] |= 1 << bitIndex;
			}

#ifdef DEBUG_TYPEIDLISTS
			typeIds = tids;
			makeVectorUniqueAndSorted(typeIds);
#endif
		}

		bool operator==(const typeIdList& rhs) const
		{
			return bitField == rhs.bitField;
		}

		bool operator!=(const typeIdList& rhs) const
		{
			return !(*this == rhs);
		}

		void addTypes(const std::vector<typeId>& tids)
		{
			for (auto tid : tids)
			{
				setBit(tid->index);
			}

#ifdef DEBUG_TYPEIDLISTS
			typeIds.insert(typeIds.end(), tids.begin(), tids.end());
			makeVectorUniqueAndSorted(typeIds);
#endif
		}

		void addTypes(const typeIdList& typesToAdd)
		{
			for (size_t i = 0; i < bitField.size(); i++)
			{
				bitField[i] = bitField[i] | (typesToAdd.bitField[i]);
			}

#ifdef DEBUG_TYPEIDLISTS
			typeIds.insert(typeIds.end(), typesToAdd.typeIds.begin(), typesToAdd.typeIds.end());
			makeVectorUniqueAndSorted(typeIds);
#endif
		}

		void deleteTypes(const std::vector<typeId>& tids)
		{
			for (auto t : tids)
			{
				clearBit(t->index);
			}

#ifdef DEBUG_TYPEIDLISTS
			for (auto t : tids)
			{
				auto it = std::find(typeIds.begin(), typeIds.end(), t);
				if (it != typeIds.end())
				{
					typeIds.erase(it);
				}
			}
#endif
		}

		void deleteTypes(const typeIdList& typesToDelete)
		{
			for (size_t i = 0; i < bitField.size(); i++)
			{
				bitField[i] = bitField[i] & (~typesToDelete.bitField[i]);
			}

#ifdef DEBUG_TYPEIDLISTS
			for (auto t : typesToDelete.typeIds)
			{
				auto it = std::find(typeIds.begin(), typeIds.end(), t);
				if (it != typeIds.end())
				{
					typeIds.erase(it);
				}
			}
#endif
		}

		bool hasAllTypes(const typeIdList& requiredTypes) const
		{
			auto& reqBitField = requiredTypes.getBitfield();
			for (size_t i = 0; i < bitField.size(); i++)
			{
				uint8_t byteToCheck = bitField[i];
				uint8_t byteReq = reqBitField[i];
				if ((byteToCheck & byteReq) != byteReq)
					return false;
			}
			return true;
		}

		bool hasType(const typeId& type)
		{
			return getBit(type->index);
		}

		typeIdList createTypeListStateComponentsOnly(const std::vector<typeId>& allRegisteredTypeIds) const
		{
			typeIdList ret(allRegisteredTypeIds.size(), {});

			for (size_t i = 0; i < allRegisteredTypeIds.size(); i++)
			{
				size_t byteIndex = i / 8;
				size_t bitIndex = i % 8;
				bool hasType = bitField[byteIndex] & (1 << bitIndex);
				if (hasType)
				{
					auto componentType = allRegisteredTypeIds[i]->type;
					if (componentType == ComponentType::State)
						ret.bitField[byteIndex] |= 1 << bitIndex;
				}
			}

#ifdef DEBUG_TYPEIDLISTS
			ret.typeIds = ret.calcTypeIds(allRegisteredTypeIds);
#endif
			return ret;
		}

		typeIdList createTypeListWithOnlySavedComponents(const std::vector<typeId>& allRegisteredTypeIds) const
		{
			typeIdList ret(allRegisteredTypeIds.size(), {});

			for (size_t i = 0; i < allRegisteredTypeIds.size(); i++)
			{
				size_t byteIndex = i / 8;
				size_t bitIndex = i % 8;
				bool hasType = bitField[byteIndex] & (1 << bitIndex);
				if (hasType)
				{
					auto componentType = allRegisteredTypeIds[i]->type;
					if (componentType == ComponentType::DontSave || componentType == ComponentType::State)
						continue;
					ret.bitField[byteIndex] |= 1 << bitIndex;
				}
			}

#ifdef DEBUG_TYPEIDLISTS
			ret.typeIds = ret.calcTypeIds(allRegisteredTypeIds);
#endif

			return ret;
		}

		const std::array<uint8_t, 4>& getBitfield() const { return bitField; }

		std::vector<typeId> calcTypeIds(const std::vector<typeId>& allRegisteredTypeIds) const 
		{
			std::vector<typeId> ret;
			for (int i = 0; i < (int)allRegisteredTypeIds.size(); i++)
			{
				if (getBit(i))
				{
					ret.push_back(allRegisteredTypeIds[i]);
				}
			}
			return ret;
		}

		bool isEmpty() const
		{
			for (auto& b : bitField)
			{
				if (b != 0)
					return false;
			}
			return true;
		}

		size_t calcTypeCount() const
		{
			size_t ret = 0;
			for (auto& b : bitField)
			{
				for (uint8_t i = 0; i < 8; i++)
				{
					if (b & (1 << i))
						ret++;
				}
			}
			return ret;
		}

		void save(std::ostream& stream) const
		{
			size_t count = bitField.size();
			stream.write((const char*)&count, sizeof(count));
			stream.write((const char*)bitField.data(), bitField.size() * sizeof(uint8_t));
		}

		void load(std::istream& stream, const std::vector<typeId>& typeIdsByLoadedIndex)
		{
			size_t s;
			stream.read((char*)&s, sizeof(s));

			if (!s)
				return;

			std::vector<uint8_t> loadedBitfield(s);
			stream.read((char*)loadedBitfield.data(), s * sizeof(uint8_t));

			for (int i = 0; i < (int)typeIdsByLoadedIndex.size(); i++)
			{
				int loadByteIndex = i / 8;
				int loadBitIndex = i % 8;
				if (loadedBitfield[loadByteIndex] & (1 << loadBitIndex))
				{
					int currentIndex = typeIdsByLoadedIndex[i]->index;
					int currentByteIndex = currentIndex / 8;
					int currentBitIndex = currentIndex % 8;
					bitField[currentByteIndex] |= 1 << currentBitIndex;

#ifdef DEBUG_TYPEIDLISTS
					typeIds.push_back(typeIdsByLoadedIndex[i]);
#endif
				}
			}
		}

	private:
		bool getBit(int typeIndex) const
		{
			int byteIndex = typeIndex / 8;
			int bitIndex = typeIndex % 8;
			return bitField[byteIndex] & (1 << bitIndex);
		}

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

		//std::vector<uint8_t> bitField;
		std::array<uint8_t, 4> bitField = {};

#ifdef DEBUG_TYPEIDLISTS
		std::vector<typeId> typeIds;
#endif
	};

	struct TypeQueryItem
	{
		enum class Mode { Read, Write, Exclude, Required };
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

		void add(const typeIdList& tids, TypeQueryItem::Mode mode)
		{
			if (mode != TypeQueryItem::Mode::Exclude)
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

	struct DontSaveEntity {};
	struct DeletedEntity {};;
}