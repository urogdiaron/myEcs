#pragma once
#include "view.h"
#include "thread_pool.h"
#include <functional>

namespace ecs
{
	struct Scheduler
	{
		Scheduler()
			: threadPool(4)
		{}

		template <class Fn, class... Ts>
		void add(View<Ts...>&& view, Fn&& job)
		{
			EASY_FUNCTION();
			if (singleThreadedMode)
			{
				view.initializeData();
				for (int i = 0; i < (int)view.queriedChunks_.size(); i++)
				{
					for (auto it = view.beginForChunk(i); it != view.endForChunk(); ++it)
					{
						std::apply(job, *it);
					}
				}
				return;
			}


			view.initializeData();
			int chunkCount = (int)view.queriedChunks_.size();
			for (int i = 0; i < chunkCount; i++)
			{
				auto result = threadPool.enqueue(
					//[viewMoved = std::move(view), jobMoved = std::move(job)]()
					[view = std::move(view), job = std::move(job), i]() mutable
				{
					EASY_BLOCK("Scheduled task");
					for (auto it = view.beginForChunk(i); it != view.endForChunk(); ++it)
					{
						std::apply(job, *it);
					}
				});
				results.push_back(std::move(result));
			}
		}

		void waitAll()
		{
			EASY_FUNCTION();
			for (auto& result : results)
				result.wait();

			results.clear();
		}

		ThreadPool threadPool;
		std::vector<std::future<void>> results;
		bool singleThreadedMode = false;
	};
}
