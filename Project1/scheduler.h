#pragma once
#include "view.h"
#include "ftl/atomic_counter.h"
#include "ftl/task_scheduler.h"
#include <functional>

namespace ecs
{
	struct Scheduler
	{
		Scheduler(Ecs* ecs)
			: ecs(ecs)
		{
			taskScheduler.Init({ 400, 0, ftl::EmptyQueueBehavior::Spin });
		}

		template<class Fn, class... Ts>
		static ftl::TaskFunction createTaskFunction()
		{
			auto fn = [](ftl::TaskScheduler* taskScheduler, void* arg) -> void
			{
				auto tuple = reinterpret_cast<std::tuple<View<Ts...>*, int, Fn*, const char*>*>(arg);
				auto view = std::get<0>(*tuple);
				auto iChunk = std::get<1>(*tuple);
				auto job = std::get<2>(*tuple);
				auto name = std::get<3>(*tuple);

				char blockName[64];
				sprintf_s(blockName, "Task MT %s", name);
				EASY_NONSCOPED_BLOCK(blockName);

				for (auto it = view->beginForChunk(iChunk); it != view->endForChunk(); ++it)
				{
					(*job)(it);
				}

				EASY_END_BLOCK;
			};

			return fn;
		}

		template<class TSystem>
		int scheduleSystem(int systemGroupIndex = -1);
		void runSystems(bool waitAll = true);

		template <class Fn, class... Ts>
		void addTask(ftl::AtomicCounter* counter, View<Ts...>* view, Fn* job, const char* name)		// Called from a fiber
		{
			view->initializeData();
			int chunkCount = (int)view->queriedChunks_.size();
			if (!chunkCount)
				return;

			{
				EASY_BLOCK("Adding chunk tasks");
				std::vector<ftl::Task> tasks(chunkCount);
				for (int i = 0; i < chunkCount; i++)
				{
					auto argTuple = std::make_tuple(view, i, job, name);
					int bufferIndex = currentBufferIndex.fetch_add(sizeof(argTuple));
					if (bufferIndex == 0)
					{
						printf("Buffer index for jobs is 0.");
					}
					uint8_t* buffer = &argBuffer[bufferIndex];
					memcpy(buffer, &argTuple, sizeof(argTuple));

					tasks[i].ArgData = buffer;
					tasks[i].Function = Scheduler::createTaskFunction<Fn, Ts...>();
				}
				//printf("\t<%d\n", counterIndex);
				taskScheduler.AddTasks(chunkCount, tasks.data(), counter);
			}
		}

		void waitCounter(ftl::AtomicCounter* counter, bool fromMainThread = false)
		{
			if (counter->Load())
			{
				//printf(">%d\n", counterIndex);
				taskScheduler.WaitForCounter(counter, 0, fromMainThread);
				if (counter->Load() > 0)
				{
					printf("This is impossible, MainThread: %d", (int)fromMainThread);
				}
			}
		}

		Ecs* ecs;
		ftl::TaskScheduler taskScheduler;

		std::mutex argBufferMutex;
		std::array<uint8_t, (1 << 20)> argBuffer; // 1 MB
		std::atomic<int> currentBufferIndex = 0;
		int currentSystemGroupIndex = 0;

		std::vector<std::unique_ptr<struct System>> systems;
		bool singleThreadedMode = false;
	};

	enum class JobState { None, Running, Done };


	template<class... Ts>
	struct Job
	{
		using Arg = View<Ts...>::iterator<true>;

		Job(View<Ts...>&& view)
			: view(std::move(view))
		{
		}

		~Job()
		{
			if (state != JobState::Done)
			{
				printf("Job destructor before finishing!");
			}
		}

		std::function<void(const Arg&)> fn;
		View<Ts...> view;
		JobState state = JobState::None;
	};

#define JOB_SET_FN(jobVariable) jobVariable.fn = [&](const decltype(jobVariable)::Arg& it)
#define JOB_SCHEDULE(jobVariable) scheduleJob(jobVariable, #jobVariable)

	struct System
	{
		virtual void scheduleJobs(ecs::Ecs* ecs) = 0;

		template<class... Ts>
		void scheduleJob(Job<Ts...>& job, const char* name = "")
		{
			if (scheduler->singleThreadedMode)
			{
				job.view.initializeData();
				int chunkCount = (int)job.view.queriedChunks_.size();

				for (int iChunk = 0; iChunk < chunkCount; iChunk++)
				{
					for (auto it = job.view.beginForChunk(iChunk); it != job.view.endForChunk(); ++it)
					{
						job.fn(it);
					}
				}
				job.state = JobState::Done;
				return;
			}

			job.state = JobState::Running;
			ftl::AtomicCounter counter(&scheduler->taskScheduler);
			scheduler->addTask(&counter, &job.view, &job.fn, name);
			scheduler->waitCounter(&counter);
			job.state = JobState::Done;
		}

		Ecs* ecs;
		Scheduler* scheduler;
		int systemGroupIndex;
	};

	template<class TSystem>
	int Scheduler::scheduleSystem(int systemGroupIndex)
	{
		auto& systemPtr = systems.emplace_back(std::make_unique<TSystem>());
		System* system = systemPtr.get();

		if (systemGroupIndex < 0)
			systemGroupIndex = currentSystemGroupIndex++;

		system->ecs = ecs;
		system->scheduler = this;
		system->systemGroupIndex = systemGroupIndex;

		return systemGroupIndex;
	}

//#pragma optimize("", off)
	void Scheduler::runSystems(bool wait)
	{
		if (singleThreadedMode)
		{
			for (auto& system : systems)
			{
				system->scheduleJobs(ecs);
			}

			return;
		}

		auto fnWrapperTask = [](ftl::TaskScheduler* taskScheduler, void* args)
		{
			auto& [scheduler, systemGroupIndex] = *reinterpret_cast<std::tuple<Scheduler*, int>*>(args);
			for (auto& system : scheduler->systems)
			{
				if (system->systemGroupIndex == systemGroupIndex)
				{
					system->scheduleJobs(scheduler->ecs);
				}
			}
		};

		std::vector<int> uniqueGroupIndices;
		ftl::AtomicCounter groupCounters[] = {
			ftl::AtomicCounter(&taskScheduler), ftl::AtomicCounter(&taskScheduler),
			ftl::AtomicCounter(&taskScheduler), ftl::AtomicCounter(&taskScheduler)
		};

		for (auto& system : systems)
		{
			auto it = std::find(uniqueGroupIndices.begin(), uniqueGroupIndices.end(), system->systemGroupIndex);
			if (it == uniqueGroupIndices.end())
			{
				uniqueGroupIndices.push_back(system->systemGroupIndex);
			}
		}

		int iGroup = 0;
		for (int groupIndex : uniqueGroupIndices)
		{
			auto argTuple = std::make_tuple(this, groupIndex);
			int bufferIndex = currentBufferIndex.fetch_add(sizeof(argTuple));
			uint8_t* buffer = &argBuffer[bufferIndex];
			memcpy(buffer, &argTuple, sizeof(argTuple));

			ftl::Task task;
			task.ArgData = buffer;
			task.Function = fnWrapperTask;
			//printf("<%d\n", groupIndex);
			taskScheduler.AddTasks(1, &task, &groupCounters[iGroup]);
			iGroup++;
		}

		for (auto& groupCounter : groupCounters)
		{
			waitCounter(&groupCounter, true);
		}

		currentSystemGroupIndex = 0;
		currentBufferIndex = 0;

		systems.clear();
		ecs->executeCommmandBuffer();
	}
//#pragma optimize("", on)

}
