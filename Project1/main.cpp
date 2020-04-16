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

	for (int i = 0; i < 5; i++)
	{
		e.createEntity<pos, hp>();// (pos{ 100, 0, 0 }, hp{ 0 });
		e.createEntity(sensor{ 5.0f });
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
	//e.changeComponents(2, { e.getTypeIdByName("position"), e.getTypeIdByName("health") });
	printf("entity #2 sensor deleted and health added\n");
	e.printArchetypes();

	ecs::View<pos> positions(e);
	for (auto& [id, p] : positions)
	{
		printf("id: %d; pos.x: %d\n", id, p.x);
		if (p.x > 15)
		{
			ecs::entityId newId = positions.createEntity<pos, hp>({}, {});
			//positions.changeComponents(newId, ecs::getTypes<pos, hp, sensor>());
			positions.setComponentData(newId, pos{ p.x * 2, p.y * 3, p.z * 4 });
			printf("new object created at id: %d; pos.x: %d\n", id, p.x);
		}
	}
	positions.executeCommmandBuffer();
	e.printArchetypes();

	for (auto& [id, pos, hp] : ecs::View<pos, hp>(e))
	{
		printf("id: %d; pos.x: %d; pos.y: %d; pos.z: %d; hp: %d\n", id, pos.x, pos.y, pos.z, hp.health);
	}
}
