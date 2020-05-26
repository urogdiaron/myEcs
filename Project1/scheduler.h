#pragma once
#include "view.h"
#include <functional>

namespace ecs
{
	struct Scheduler
	{
		template <class Fn, class... Ts>
		void add(View<Ts...> view, Fn job)
		{
			for (auto i : view)
			{
				std::apply(job, i);
			}
		}
	};
}
