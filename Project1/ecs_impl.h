#pragma once
#include "ecs.h"

namespace ecs
{
	Ecs::Ecs()
	{
		registerType<DontSaveEntity>("DontSaveEntity", ComponentType::Internal);
		registerType<DeletedEntity>("DeletedEntity", ComponentType::Internal);
	}
	
	std::tuple<int, Archetype*> Ecs::createArchetype(const typeIdList& typeIds)
	{
		int i = 0;
		for (auto& itArchetype : archetypes_)
		{
			if (itArchetype->containedTypes_ == typeIds)
			{
				return { i, itArchetype.get() };
			}
			i++;
		}

		auto& insertedItem = archetypes_.emplace_back(std::make_unique<Archetype>(typeIds, this));
		return { i, insertedItem.get() };
	}
	
	entityId Ecs::createEntity_impl(const typeIdList& typeIds)
	{
		entityId newEntityId = nextEntityId++;
		auto [archIndex, archetype] = createArchetype(typeIds);
		int elementIndex = archetype->createEntity(newEntityId);
		entityDataIndexMap_[newEntityId] = { archIndex, elementIndex };
		return newEntityId;
	}

	// If you call this from the outside, keepStateComponents needs to be true. False is only for internal usage.
	bool Ecs::deleteEntity(entityId id, bool keepStateComponents)
	{
		auto it = entityDataIndexMap_.find(id);
		if (it == entityDataIndexMap_.end())
			return false;

		Archetype* arch = archetypes_[it->second.archetypeIndex].get();

		if (keepStateComponents)
		{
			// Check if the archetype had State type components
			// If it did, don't actually delete the entity. Keep the state components.
			for (auto typeDesc : arch->containedTypes_.calcTypeIds(typeIds_))
			{
				if (typeDesc->type == ComponentType::State)
				{
					typeIdList stateComponentsOnly = arch->containedTypes_.createTypeListStateComponentsOnly(typeIds_);
					stateComponentsOnly.addTypes({ getTypeId<DeletedEntity>() });
					changeComponents(id, stateComponentsOnly);
					return true;
				}
			}
		}

		// Delete the entity and clean up the map indices
		arch->deleteEntity(it->second.elementIndex);

		for (int i = it->second.elementIndex; i < (int)arch->entityIds_.size(); i++)
		{
			entityId changedId = arch->entityIds_[i];
			auto changedIt = entityDataIndexMap_.find(changedId);
			if (changedIt != entityDataIndexMap_.end())
			{
				changedIt->second.elementIndex = i;
			}
		}

		entityDataIndexMap_.erase(it);

		return true;
	}
	
	void Ecs::deleteComponents(entityId id, const typeIdList& typeIds)
	{
		auto it = entityDataIndexMap_.find(id);
		if (it == entityDataIndexMap_.end())
			return;

		Archetype* oldArchetype = archetypes_[it->second.archetypeIndex].get();
		typeIdList remainingTypes = oldArchetype->containedTypes_;
		remainingTypes.deleteTypes(typeIds);
		changeComponents(id, remainingTypes);
	}
	
	void Ecs::changeComponents(entityId id, const typeIdList& typeIds)
	{
		auto it = entityDataIndexMap_.find(id);
		if (it == entityDataIndexMap_.end())
			return;

		Archetype* oldArchetype = archetypes_[it->second.archetypeIndex].get();
		auto newTypes = typeIds;

		size_t typeCount = newTypes.calcTypeCount();
		if (typeCount == 0 ||
			(typeCount == 1 && newTypes.hasType(getTypeId<DeletedEntity>())))
		{	// we deleted all the components or only the deleted entity field left
			deleteEntity(id, false);
			return;
		}

		auto [archIndex, archetype] = createArchetype(newTypes);
		if (archetype == oldArchetype)
			return;

		int newElementIndex = archetype->copyFromEntity(id, it->second.elementIndex, oldArchetype);
		deleteEntity(id, false);
		entityDataIndexMap_[id] = { archIndex, newElementIndex };
	}
	
	typeId Ecs::getTypeIdByName(const std::string& typeName)
	{
		auto it = std::find_if(typeDescriptors_.begin(), typeDescriptors_.end(), [&](const std::unique_ptr<TypeDescriptor>& td) -> bool
			{
				return td->name == typeName;
			});

		if (it != typeDescriptors_.end())
			return it->get();

		return nullptr;
	}
	
	void Ecs::save(std::ostream& stream) const
	{
#if 0
		size_t count = typeDescriptors_.size();
		stream.write((char*)&count, sizeof(size_t));
		for (auto& t : typeDescriptors_)
		{
			stream.write((const char*)&t->index, sizeof(t->index));
			count = t->name.size();
			stream.write((char*)&count, sizeof(size_t));
			stream.write(t->name.data(), count);
		}

		// Collect all archetypes that are equivalent even after disregarding state components
		auto entityMapCopy = std::unordered_map<entityId, entityDataIndex>();

		int archetypeIndex = 0;
		std::vector<uint8_t> skipArchetype(archetypes_.size());
		typeId dontSaveEntityType = getTypeId<DontSaveEntity>();

		for (size_t iArch = 0; iArch < archetypes_.size(); iArch++)
		{
			if (archetypes_[iArch]->containedTypes_.hasType(dontSaveEntityType))
				skipArchetype[iArch] = 1;
		}

		for (size_t iArch = 0; iArch < archetypes_.size(); iArch++)
		{
			if (skipArchetype[iArch])
				continue;
			auto archetype = archetypes_[iArch].get();
			typeIdList typeIdsToSave = archetype->containedTypes_.createTypeListWithOnlySavedComponents(typeIds_);
			if (typeIdsToSave.isEmpty())
				continue;	// don't save empty archetypes

			std::vector<const Archetype*> archetypesToSave = { archetype };
			for (size_t iArchToCheck = iArch + 1; iArchToCheck < archetypes_.size(); iArchToCheck++)
			{
				if (skipArchetype[iArch])
					continue;
				auto archetypeToMerge = archetypes_[iArchToCheck].get();
				typeIdList typeIdsToCheck = archetypeToMerge->containedTypes_.createTypeListWithOnlySavedComponents(typeIds_);
				if (typeIdsToSave != typeIdsToCheck)
					continue;

				// These two archetypes are the same without state components
				skipArchetype[iArchToCheck] = 1;
				archetypesToSave.push_back(archetypeToMerge);
			}

			size_t entityCount = 0;
			for (auto arch : archetypesToSave)
			{
				for (auto& entity : arch->entityIds_)
				{
					auto& entityMapEntry = entityMapCopy[entity];
					entityMapEntry.archetypeIndex = archetypeIndex;
					entityMapEntry.elementIndex = (int)entityCount;
					entityCount++;
				}
			}

			if (entityCount == 0)
				continue;

			archetypeIndex++;

			typeIdsToSave.save(stream);
			stream.write((const char*)&entityCount, sizeof(entityCount));

			for (auto arch : archetypesToSave)
			{
				stream.write((const char*)arch->entityIds_.data(), arch->entityIds_.size() * sizeof(entityId));
			}

			auto typesSortedByIndex = typeIdsToSave.calcTypeIds(typeIds_);
			for (auto t : typesSortedByIndex)
			{
				for (auto arch : archetypesToSave)
				{
					auto it = arch->componentArrays_.find(t);
					it->second->save(stream);
				}
			}
		}

		// write out an empty typeidlist to signal end of archetypes
		count = 0;
		stream.write((char*)&count, sizeof(size_t));

		count = entityMapCopy.size();
		stream.write((char*)&count, sizeof(size_t));
		for (auto& it : entityMapCopy)
		{
			stream.write((const char*)&it.first, sizeof(it.first));
			stream.write((char*)&it.second, sizeof(it.second));
		}

		stream.write((char*)&nextEntityId, sizeof(nextEntityId));
#endif
	}
	
	void Ecs::load(std::istream& stream)
	{
#if 0
		entityDataIndexMap_.clear();
		archetypes_.clear();
		entityCommandBuffer_.clear();
		nextEntityId = 1;

		size_t typeDescCount = 0;
		stream.read((char*)&typeDescCount, sizeof(typeDescCount));
		std::vector<typeId> typeIdsByLoadedIndex(typeDescCount);
		for (size_t i = 0; i < typeDescCount; i++)
		{
			TypeDescriptor td;
			stream.read((char*)&td.index, sizeof(td.index));
			size_t count;
			stream.read((char*)&count, sizeof(count));
			td.name.resize(count);
			stream.read(td.name.data(), count);

			typeId id = getTypeIdByName(td.name);
			typeIdsByLoadedIndex[td.index] = id;
		}

		while (true)
		{
			typeIdList loadedTypeIds = getTypeIds<>();
			loadedTypeIds.load(stream, typeIdsByLoadedIndex);

			if (loadedTypeIds.isEmpty())
				break;

			auto& itArch = archetypes_.emplace_back(std::make_unique<Archetype>());
			itArch->load(stream, loadedTypeIds, typeIds_, componentArrayFactory_);
		}

		size_t entityCount = 0;
		stream.read((char*)&entityCount, sizeof(entityCount));
		for (size_t i = 0; i < entityCount; i++)
		{
			entityId id;
			stream.read((char*)&id, sizeof(id));
			entityDataIndex dataIndex;
			stream.read((char*)&dataIndex, sizeof(dataIndex));
			entityDataIndexMap_[id] = dataIndex;
		}

		stream.read((char*)&nextEntityId, sizeof(nextEntityId));
#endif
	}
}
