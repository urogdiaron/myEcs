#include "view.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <stdio.h>
#include <tuple>


namespace ecs
{
}

struct pos
{
	int x, y, z;
};

struct hp
{
	int health = 100;
};

struct sensor
{
	float radius = 5.0f;
};

void main()
{
	ecs::Ecs e;
	e.registerType<pos>("position");
	e.registerType<hp>("health");
	e.registerType<sensor>("sensor");

	std::vector<ecs::typeId> ph = { e.getTypeIdByName("position"), e.getTypeIdByName("health") };
	std::sort(ph.begin(), ph.end());
	
	std::vector<ecs::typeId> ps = { e.getTypeIdByName("position"), e.getTypeIdByName("sensor") };
	std::sort(ps.begin(), ps.end());

	for (int i = 0; i < 5; i++)
	{
		e.createEntity(ph);
		e.createEntity(ps);
	}

	for (auto& [id, pos] : ecs::View<pos>(e))
	{
		pos.x += id * 5;
	}

	for (auto& [id, pos, hp] : ecs::View<pos, hp>(e))
	{
		printf("id: %d; pos.x: %d; hp: %d\n", id, pos.x, hp.health);
	}

	e.deleteEntity(5);
	printf("entity #5 is deleted\n");

	e.deleteEntity(6);
	printf("entity #6 is deleted\n");

	e.deleteEntity(7);
	printf("entity #7 is deleted\n");

	e.printArchetypes();
	e.changeComponents(2, { e.getTypeIdByName("position"), e.getTypeIdByName("health") });
	printf("entity #2 sensor deleted and health added\n");
	e.printArchetypes();

	ecs::View<pos> positions(e);
	for (auto& [id, p] : positions)
	{
		printf("id: %d; pos.x: %d\n", id, p.x);
		if (p.x > 15)
		{
			// csinalhatnank egy kis local ecs-t itt, ami mindent belerakunk. az execute pedig mergelne az eredetibe
			ecs::entityId newId = positions.createEntity(ecs::getTypes<pos, hp>());
			positions.changeComponents(newId, ecs::getTypes<pos, hp, sensor>());
			printf("new object created at id: %d; pos.x: %d\n", id, p.x);
		}
	}

	for (auto& [id, pos, hp] : ecs::View<pos, hp>(e))
	{
		printf("id: %d; pos.x: %d; hp: %d\n", id, pos.x, hp.health);
	}
}
