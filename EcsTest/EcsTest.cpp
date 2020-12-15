#include <vector>

//#define BUILD_WITH_EASY_PROFILER
#define EASY_OPTION_START_LISTEN_ON_STARTUP 1
#define EASY_OPTION_LOG_ENABLED 1
#include "easy/profiler.h"

class MyStream
{
public:
	MyStream(void* buffer)
		: buffer(buffer)
	{}

	void read(char* data, size_t size)
	{
		memcpy(data, (char*)buffer + pos, size);
		pos += size;
	}
	void write(const char* data, size_t size)
	{
		memcpy((char*)buffer + pos, data, size);
		pos += size;
	}

	void reset()
	{
		pos = 0;
	}

private:
	void* buffer;
	size_t pos = 0;
};

using istream = MyStream;

#include "scheduler.h"
//#include "view.h"
#include <stdio.h>
#include <chrono>

struct Timer
{
	Timer(const std::string& name)
		: name(name)
	{
		start = std::chrono::high_resolution_clock::now();
	}

	~Timer()
	{
		auto end = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration<double, std::milli>(end - start);
		printf("Timer %s: %f ms\n", name.c_str(), duration.count());
	}

	std::string name;
	std::chrono::high_resolution_clock::time_point start;
};

struct A
{
	int a = 0;
};

struct B
{
	int b = 0;
	float bf = 0.0f;
};

struct C
{
	double c = 0;
	std::vector<int> cs;
};

void printAs(ecs::Ecs& ecs)
{
	for (auto it : ecs.view<A>())
	{
		auto& [id, a] = it;
		printf("A {%u, %d}\n", id, a.a);
	}

	printf("\n");
}

void printABs(ecs::Ecs& ecs, int maxCount = -1)
{
	int i = 0;

	for (auto& [id, a, b] : ecs.view<A, B>())
	{
		i++;
		if (maxCount >= 0 && i > maxCount)
			continue;

		printf("AB {%u, a: %d, b: %d, bf: %0.2f}\n", id, a.a, b.b, b.bf);
	}

	printf("\n");
}

void processAb(A& a, B& b)
{
	EASY_FUNCTION();
	a.a += b.b;
	b.bf = a.a + b.b * sqrtf(b.bf);
	for (int i = 0; i < 15; i++)
	{
		a.a += b.b;
	}
}

void increaseAbs(ecs::Ecs& ecs)
{
	EASY_FUNCTION();
	for (auto& [id, a, b] : ecs.view<A, B>())
	{
		processAb(a, b);
	}
}

struct IncreaseAbs : ecs::System
{
	void scheduleJobs(ecs::Ecs* ecs) override
	{
		auto abView = ecs::Job(ecs->view<A, B>());
		JOB_SET_FN(abView)
		{
			auto& [id, a, b] = *it;
			processAb(a, b);
		};
		JOB_SCHEDULE(abView);
	}
};

void main()
{
	EASY_PROFILER_ENABLE;
	profiler::startListen();

	EASY_MAIN_THREAD;

	std::vector<char> buffer(1 << 20); // one megabyte
	MyStream stream(buffer.data());

	{
		ecs::Ecs ecs;
		ecs.registerType<A>("AComp");
		ecs.registerType<B>("BComp");

		// CREATE FROM CODE
		ecs.createEntity(A{ 2 }, B{ 2, 2.0f });

		// PREFABS
		ecs::Prefab<A> aPrefab;
		ecs::Prefab<B> bPrefab;
		ecs::Prefab<A, B> abPrefab{ A{1}, B{1, 1.0f} };

		ecs.createEntity(aPrefab);
		ecs.createEntity(bPrefab);
		ecs.createEntity(abPrefab);

		printAs(ecs);
		increaseAbs(ecs);
		printf("'AB's are increased\n");
		printABs(ecs);

		ecs.savePrefab(stream, abPrefab);
	}

	{
		printf("\n\nNEW ECS CREATED!\n\n");
		ecs::Ecs ecs;
		ecs.registerType<A>("AComp");
		ecs.registerType<B>("BComp");
		ecs.registerType<C>("CComp");

		for (int i = 0; i < 10; i++)
		{
			stream.reset();
			ecs.createEntityFromPrefabStream(stream);
		}
		printABs(ecs);

		stream.reset();
		ecs.save(stream);
	}

	{
		printf("\n\nNEW ECS CREATED!\n\n");
		ecs::Ecs ecs;
		auto scheduler = std::make_unique<ecs::Scheduler>(&ecs);

		ecs.registerType<A>("AComp");
		ecs.registerType<B>("BComp");
		ecs.registerType<C>("CComp");

		stream.reset();
		ecs.load(stream);

		printABs(ecs);


		{
			int addedCount = 10000;
			ecs::Prefab<A, B> abPrefab{ A{1}, B{1, 1.0f} };

			printf("\nAdding %d ABs!\n", addedCount);
			printABs(ecs, 10);
			EASY_BLOCK("Adding data");
			for (int i = 0; i < addedCount; i++)
			{
				ecs.createEntity(abPrefab);
			}
		}

		{
			EASY_BLOCK("MULTITHREADED");
			Timer timer("MULTITHREADED");
			for (int i = 0; i < 20; i++)
			{
				EASY_BLOCK("Scheduled Run");
				scheduler->scheduleSystem<IncreaseAbs>();
				scheduler->runSystems();
			}
		}

		{
			EASY_BLOCK("SINGLETHREADED");
			Timer timer("SINGLETHREADED");
			scheduler->singleThreadedMode = true;
			for (int i = 0; i < 20; i++)
			{
				EASY_BLOCK("Scheduled Run");
				scheduler->scheduleSystem<IncreaseAbs>();
				scheduler->runSystems();
			}
		}

		{
			Timer timer("SERIAL");
			EASY_BLOCK("SERIAL");
			for (int i = 0; i < 20; i++)
			{
				EASY_BLOCK("Serial Run");
				increaseAbs(ecs);
			}
		}

		printABs(ecs, 10);
	}

	while (true);
}
