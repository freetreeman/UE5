// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Tests/Benchmark.h"
#include "Tasks/Pipe.h"
#include "HAL/Thread.h"
#include "Async/ParallelFor.h"
#include "Experimental/Async/AwaitableTask.h"

#include <atomic>

#if WITH_DEV_AUTOMATION_TESTS

namespace UE { namespace TasksTests
{
	using namespace Tasks;

	void DummyFunc()
	{
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTasksBasicTest, "System.Core.Tasks.Basic", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter);

	template<uint32 SpawnerGroupsNum, uint32 SpawnersPerGroupNum>
	void BasicStressTest()
	{
		TArray<FTask> SpawnerGroups;
		SpawnerGroups.Reserve(SpawnerGroupsNum);

		constexpr uint32 TasksNum = SpawnerGroupsNum * SpawnersPerGroupNum;
		TArray<FTask> Spawners;
		Spawners.AddDefaulted(TasksNum);
		TArray<FTask> Tasks;
		Tasks.AddDefaulted(TasksNum);

		std::atomic<uint32> TasksExecutedNum{ 0 };

		for (uint32 GroupIndex = 0; GroupIndex != SpawnerGroupsNum; ++GroupIndex)
		{
			SpawnerGroups.Add(Launch(UE_SOURCE_LOCATION,
				[
					Spawners = &Spawners[GroupIndex * SpawnersPerGroupNum],
					Tasks = &Tasks[GroupIndex * SpawnersPerGroupNum],
					&TasksExecutedNum
				]
				{
					for (uint32 SpawnerIndex = 0; SpawnerIndex != SpawnersPerGroupNum; ++SpawnerIndex)
					{
						Spawners[SpawnerIndex] = Launch(UE_SOURCE_LOCATION,
							[
								Task = &Tasks[SpawnerIndex],
								&TasksExecutedNum
							]
							{
								*Task = Launch(UE_SOURCE_LOCATION,
									[&TasksExecutedNum]
									{
										++TasksExecutedNum;
									}
								);
							}
						);
					}
				}
			));
		}

		Wait(SpawnerGroups);
		Wait(Spawners);
		Wait(Tasks);

		check(TasksExecutedNum == TasksNum);
	}

	bool FTasksBasicTest::RunTest(const FString& Parameters)
	{
		if (!FPlatformProcess::SupportsMultithreading())
		{
			// the new API doesn't support single-threaded execution (`-nothreading`) until it's feature-compatible with the old API and completely replaces it
			return true;
		}

		{	// basic example, fire and forget a high-pri task
			Launch(
				UE_SOURCE_LOCATION, // debug name
				[] {}, // task body
				Tasks::ETaskPriority::High /* task priority, `Normal` by default */
			);
		}

		{	// launch a task and wait till it's executed
			Launch(UE_SOURCE_LOCATION, [] {}).Wait();
			Launch(UE_SOURCE_LOCATION, [] {}).BusyWait();
		}

		{	// FTaskEvent
			FTaskEvent Event{ UE_SOURCE_LOCATION };
			check(!Event.IsCompleted());

			// check that waiting blocks
			FTask Task = Launch(UE_SOURCE_LOCATION, [&Event] { Event.Wait(); });
			FPlatformProcess::Sleep(0.1f);
			check(!Task.IsCompleted());

			Event.Trigger();
			check(Event.IsCompleted());
			verify(Event.Wait(FTimespan::Zero()));
			verify(Event.BusyWait(FTimespan::Zero()));
		}

		{	// postpone execution so waiting kicks in first
			std::atomic<int32> Counter{ 0 };
			FTask Task = Launch(UE_SOURCE_LOCATION, [&Counter] { ++Counter; FPlatformProcess::Sleep(0.1f); });

			ensure(!Task.Wait(FTimespan::Zero()));
			Task.Wait();
			check(Counter == 1);
		}

		{	// postpone execution so busy waiting kicks in first
			std::atomic<int32> Counter{ 0 };
			FTask Task = Launch(UE_SOURCE_LOCATION, [&Counter] { ++Counter; FPlatformProcess::Sleep(0.1f); });

			ensure(!Task.BusyWait(FTimespan::Zero()));
			Task.BusyWait();
			check(Counter == 1);
		}

		{	// same but using `FTaskEvent`
			FTaskEvent Event{ UE_SOURCE_LOCATION };
			FTask Task = Launch(UE_SOURCE_LOCATION, [&Event] { Event.Wait(); });
			ensure(!Task.Wait(FTimespan::FromMilliseconds(100)));
			Event.Trigger();
			Task.Wait();
		}

		{	// same but using busy-wait and `FTaskEvent`
			FTaskEvent Event{ UE_SOURCE_LOCATION };
			FTask Task = Launch(UE_SOURCE_LOCATION, [&Event] { Event.BusyWait(); });
			ensure(!Task.BusyWait(FTimespan::FromMilliseconds(100)));
			Event.Trigger();
			Task.BusyWait();
		}

		{	// basic use-case, postpone waiting so the task is executed first
			std::atomic<bool> Done{ false };
			FTask Task = Launch(UE_SOURCE_LOCATION, [&Done] { Done = true; });
			while (!Task.IsCompleted())
			{
				FPlatformProcess::Yield();
			}
			Task.Wait();
			check(Done);
		}

		{	// basic use-case, postpone busy-waiting so the task is executed first
			std::atomic<bool> Done{ false };
			FTask Task = Launch(UE_SOURCE_LOCATION, [&Done] { Done = true; });
			while (!Task.IsCompleted())
			{
				FPlatformProcess::Yield();
			}
			Task.BusyWait();
			check(Done);
		}

		{	// basic use-case with result, postpone execution so waiting kicks in first
			TTask<int> Task = Launch(UE_SOURCE_LOCATION, [] { FPlatformProcess::Sleep(0.1f);  return 42; });
			verify(Task.GetResult() == 42);
		}

		{	// basic use-case with result, postpone waiting so the task is executed first
			TTask<int> Task = Launch(UE_SOURCE_LOCATION, [] { return 42; });
			while (!Task.IsCompleted())
			{
				FPlatformProcess::Yield();
			}
			verify(Task.GetResult() == 42);
		}

		{	// check that movable-only result types are supported, that only single instance of result is created and that it's destroyed
			static std::atomic<uint32> ConstructionsNum{ 0 }; // `static` to make it visible to `FMoveConstructable`
			static std::atomic<uint32> DestructionsNum{ 0 };

			struct FMoveConstructable
			{
				FORCENOINLINE FMoveConstructable()
				{
					ConstructionsNum.fetch_add(1, std::memory_order_relaxed);
				}

				FMoveConstructable(FMoveConstructable&&)
					: FMoveConstructable()
				{
				}

				FMoveConstructable(const FMoveConstructable&) = delete;
				FMoveConstructable& operator=(FMoveConstructable&&) = delete;
				FMoveConstructable& operator=(const FMoveConstructable&) = delete;

				FORCENOINLINE ~FMoveConstructable()
				{
					DestructionsNum.fetch_add(1, std::memory_order_relaxed);
				}
			};

			{
				Launch(UE_SOURCE_LOCATION, [] { return FMoveConstructable{}; }).GetResult();
			}

#if 0	// unreliable test, destruction can happen on a worker thread, after the task is flagged as completed and so the check can be hit before the destruction
			uint32 LocalConstructionsNum = ConstructionsNum.load(std::memory_order_relaxed);
			uint32 LocalDestructionsNum = DestructionsNum.load(std::memory_order_relaxed);
			checkf(LocalConstructionsNum == 1, TEXT("%d result instances were created but one was expected: the value stored in the task"), LocalConstructionsNum);
			checkf(LocalConstructionsNum == LocalDestructionsNum, TEXT("Mismatched number of constructions (%d) and destructions (%d)"), LocalConstructionsNum, LocalDestructionsNum);

			ConstructionsNum = 0;
			DestructionsNum = 0;
#endif

			{
				FMoveConstructable Res{ MoveTemp(Launch(UE_SOURCE_LOCATION, [] { return FMoveConstructable{}; }).GetResult()) }; // consume the result
			}

#if 0	// unreliable test, destruction can happen on a worker thread, after the task is flagged as completed and so the check can be hit before the destruction
			LocalConstructionsNum = ConstructionsNum.load(std::memory_order_relaxed);
			LocalDestructionsNum = DestructionsNum.load(std::memory_order_relaxed);
			checkf(LocalConstructionsNum == 2, TEXT("%d result instances were created but 2 was expected: the value stored in the task"), LocalConstructionsNum);
			checkf(LocalConstructionsNum == LocalDestructionsNum, TEXT("Mismatched number of constructions (%d) and destructions (%d)"), LocalConstructionsNum, LocalDestructionsNum);

			ConstructionsNum = 0;
			DestructionsNum = 0;
#endif
		}

		// fire and forget: launch a task w/o keeping its reference
		if (LowLevelTasks::FScheduler::Get().GetNumWorkers() != 0)
		{
			std::atomic<bool> bDone{ false };
			Launch(UE_SOURCE_LOCATION, [&bDone] { bDone = true; });
			while (!bDone)
			{
				FPlatformProcess::Yield();
			}
		}

		{	// mutable lambda, compilation check
			Launch(UE_SOURCE_LOCATION, []() mutable {}).Wait();
			Launch(UE_SOURCE_LOCATION, []() mutable { return false; }).GetResult();
		}

		{	// free memory occupied by a private task instance, can be required if task instance is held as a member var
			FTask Task = Launch(UE_SOURCE_LOCATION, [] {});
			Task.Wait();
			Task = {};
		}

		UE_BENCHMARK(5, BasicStressTest<1000, 1000>);

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTasksPipeTest, "System.Core.Tasks.Pipe", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter);

	template<uint32 SpawnerGroupsNum, uint32 SpawnersPerGroupNum>
	void PipeStressTest();

	bool FTasksPipeTest::RunTest(const FString& Parameters)
	{
		if (!FPlatformProcess::SupportsMultithreading())
		{
			// the new API doesn't support single-threaded execution (`-nothreading`) until it's feature-compatible with the new API and completely replaces it
			return true;
		}

		{	// a basic usage example
			Tasks::FPipe Pipe{ UE_SOURCE_LOCATION }; // a debug name, user-provided or 
				// `UE_SOURCE_LOCATION` - source file name and line number
			// launch two tasks in the pipe, they will be executed sequentially, but in parallel
			// with other tasks (including TaskGraph's old API tasks)
			Tasks::FTask Task1 = Pipe.Launch(UE_SOURCE_LOCATION, [] {});
			Tasks::FTask Task2 = Pipe.Launch(UE_SOURCE_LOCATION, [] {});
			Task2.Wait(); // wait for `Task2` completion
		}

		{	// an example of thread-safe async interface, kind of a primitive "actor"
			class FAsyncClass
			{
			public:
				TTask<bool> DoSomething()
				{
					return Pipe.Launch(TEXT("DoSomething()"), [this] { return DoSomethingImpl(); });
				}

				FTask DoSomethingElse()
				{
					return Pipe.Launch(TEXT("DoSomethingElse()"), [this] { DoSomethingElseImpl(); });
				}

			private:
				bool DoSomethingImpl() { return false; }
				void DoSomethingElseImpl() {}

			private:
				FPipe Pipe{ UE_SOURCE_LOCATION };
			};

			// access the same instance from multiple threads
			FAsyncClass AsyncInstance;
			bool bRes = AsyncInstance.DoSomething().GetResult();
			AsyncInstance.DoSomethingElse().Wait();
		}

		{	// basic
			FPipe Pipe{ UE_SOURCE_LOCATION };
			Pipe.Launch(UE_SOURCE_LOCATION, [] {});
			Pipe.Launch(UE_SOURCE_LOCATION, [] {}).Wait();
		}

		{	// launching a piped task with pointer to a function
			FPipe Pipe{ UE_SOURCE_LOCATION };
			Pipe.Launch(UE_SOURCE_LOCATION, &DummyFunc).Wait();
		}

		{	// launching a piped task with a functor object
			struct FFunctor
			{
				void operator()()
				{
				}
			};

			FPipe Pipe{ UE_SOURCE_LOCATION };
			Pipe.Launch(UE_SOURCE_LOCATION, FFunctor{}).Wait();
		}

		{	// hold the first piped task execution until the next one is piped to test for non-concurrent execution
			FPipe Pipe{ UE_SOURCE_LOCATION };
			bool bTask1Done = false;
			FTask Task1 = Pipe.Launch(UE_SOURCE_LOCATION, 
				[&bTask1Done] 
				{ 
					FPlatformProcess::Sleep(0.1f); 
					bTask1Done = true; 
				}
			);
			// we can't just check if `Task1` is completed because pipe gets unblocked and so the next piped task can start execution before the
			// previous piped task's comlpetion flag is set
			Pipe.Launch(UE_SOURCE_LOCATION, [&bTask1Done] { check(bTask1Done); }).Wait();
		}

		{	// piping another task after the previous one is completed and destroyed
			FPipe Pipe{ UE_SOURCE_LOCATION };

			Pipe.Launch(UE_SOURCE_LOCATION, [] {}).Wait();
			Pipe.Launch(UE_SOURCE_LOCATION, [] {}).Wait();
		}

		{	// an example of blocking a pipe
			FPipe Pipe{ UE_SOURCE_LOCATION };
			std::atomic<bool> bBlocked;
			FTaskEvent Event{ UE_SOURCE_LOCATION };
			FTask Task = Pipe.Launch(UE_SOURCE_LOCATION,
				[&bBlocked, &Event]
				{
					bBlocked = true;
					Event.Wait(); 
				}
			);
			while (!bBlocked)
			{
			}
			// now it's blocked
			ensure(!Task.Wait(FTimespan::FromMilliseconds(100)));

			Event.Trigger(); // unblock
			Task.Wait();
		}

		UE_BENCHMARK(5, PipeStressTest<500, 500>);

		return true;
	}

	// stress test for named thread tasks, checks spawning a large number of tasks from multiple threads and executing them
	template<uint32 SpawnerGroupsNum, uint32 SpawnersPerGroupNum>
	void PipeStressTest()
	{
		TArray<FTask> SpawnerGroups;
		SpawnerGroups.Reserve(SpawnerGroupsNum);

		constexpr uint32 TasksNum = SpawnerGroupsNum * SpawnersPerGroupNum;
		TArray<FTask> Spawners;
		Spawners.AddDefaulted(TasksNum);
		TArray<FTask> Tasks;
		Tasks.AddDefaulted(TasksNum);

		std::atomic<bool> bExecuting{ false };
		std::atomic<uint32> TasksExecutedNum{ 0 };

		FPipe Pipe{ UE_SOURCE_LOCATION };

		for (uint32 GroupIndex = 0; GroupIndex != SpawnerGroupsNum; ++GroupIndex)
		{
			SpawnerGroups.Add(Launch(UE_SOURCE_LOCATION,
				[
					Spawners = &Spawners[GroupIndex * SpawnersPerGroupNum],
					Tasks = &Tasks[GroupIndex * SpawnersPerGroupNum],
					&bExecuting,
					&TasksExecutedNum,
					&Pipe
				]
				{
					for (uint32 SpawnerIndex = 0; SpawnerIndex != SpawnersPerGroupNum; ++SpawnerIndex)
					{
						Spawners[SpawnerIndex] = Launch(UE_SOURCE_LOCATION,
							[
								Task = &Tasks[SpawnerIndex],
								&bExecuting,
								&TasksExecutedNum,
								&Pipe
							]
							{
								*Task = Pipe.Launch(UE_SOURCE_LOCATION,
									[&bExecuting, &TasksExecutedNum]
									{
										check(!bExecuting);
										bExecuting = true;
										++TasksExecutedNum;
										bExecuting = false;
									}
								);
							}
						);
					}
				}
				));
		}

		Wait(SpawnerGroups);
		Wait(Spawners);
		Wait(Tasks);

		check(TasksExecutedNum == TasksNum);
	}

	struct FAutoTlsSlot
	{
		uint32 Slot;

		FAutoTlsSlot()
			: Slot(FPlatformTLS::AllocTlsSlot())
		{
		}

		~FAutoTlsSlot()
		{
			FPlatformTLS::FreeTlsSlot(Slot);
		}
	};

	template<uint64 Num>
	void UeTlsStressTest()
	{
		static FAutoTlsSlot Slot;
		double Dummy = 0;
		for (uint64 i = 0; i != Num; ++i)
		{
			Dummy += (double)(uintptr_t)(FPlatformTLS::GetTlsValue(Slot.Slot));
			double Now = FPlatformTime::Seconds();
			FPlatformTLS::SetTlsValue(Slot.Slot, (void*)(uintptr_t)(Now));
		}
		FPlatformTLS::SetTlsValue(Slot.Slot, (void*)(uintptr_t)(Dummy));
	}

	template<uint64 Num>
	void ThreadLocalStressTest()
	{
		static thread_local double TlsValue;
		double Dummy = 0;
		for (uint64 i = 0; i != Num; ++i)
		{
			Dummy += TlsValue;
			double Now = FPlatformTime::Seconds();
			TlsValue = Now;
		}
		TlsValue = Dummy;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTlsTest, "System.Core.Tls", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter);

	bool FTlsTest::RunTest(const FString& Parameters)
	{
		UE_BENCHMARK(5, UeTlsStressTest<10000000>);
		UE_BENCHMARK(5, ThreadLocalStressTest<10000000>);

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTasksDependenciesTest, "System.Core.Tasks.Dependencies", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter);

	template<uint64 NumBranches, uint64 NumLoops, uint64 NumTasks>
	void DependenciesPerfTest()
	{
		auto Branch = []
		{
			FTask Joiner;
			for (uint64 LoopIndex = 0; LoopIndex != NumLoops; ++LoopIndex)
			{
				TArray<FTask> Tasks;
				Tasks.Reserve(NumTasks);
				for (uint64 TaskIndex = 0; TaskIndex != NumTasks; ++TaskIndex)
				{
					if (Joiner.IsValid())
					{
						Tasks.Add(Launch(UE_SOURCE_LOCATION, [] { /*FPlatformProcess::YieldCycles(100000);*/ }, Joiner));
					}
					else
					{
						Tasks.Add(Launch(UE_SOURCE_LOCATION, [] { /*FPlatformProcess::YieldCycles(100000);*/ }));
					}
				}
				Joiner = Launch(UE_SOURCE_LOCATION, [] {}, Tasks);
			}
			return Joiner;
		};

		TArray<TTask<FTask>> Branches;
		Branches.Reserve(NumBranches);
		for (uint64 BranchIndex = 0; BranchIndex != NumBranches; ++BranchIndex)
		{
			Branches.Add(Launch(UE_SOURCE_LOCATION, Branch));
		}
		TArray<FTask> BranchTasks;
		BranchTasks.Reserve(NumBranches);
		for (TTask<FTask>& Task : Branches)
		{
			BranchTasks.Add(Task.GetResult());
		}
		Wait(Branches);
	}

	bool FTasksDependenciesTest::RunTest(const FString& Parameters)
	{
		{	// a task is not executed until its prerequisite (FTaskEvent) is completed
			FTaskEvent Prereq{ UE_SOURCE_LOCATION };

			FTask Task{ Launch(UE_SOURCE_LOCATION, [] {}, Prereq) };
			verify(!Task.Wait(FTimespan::FromMilliseconds(10)));

			Prereq.Trigger();
			Task.Wait();
		}

		{	// a task is not executed until its prerequisite (FTaskEvent) is completed. with explicit task priority
			FTaskEvent Prereq{ UE_SOURCE_LOCATION };

			FTask Task{ Launch(UE_SOURCE_LOCATION, [] {}, Prereq, ETaskPriority::Normal) };
			verify(!Task.Wait(FTimespan::FromMilliseconds(10)));

			Prereq.Trigger();
			Task.Wait();
		}

		{	// a task is not executed until its prerequisite (FTask) is completed
			FTaskEvent Event{ UE_SOURCE_LOCATION };
			FTask Prereq{ Launch(UE_SOURCE_LOCATION, [&Event] { Event.Wait(); }) };
			FTask Task{ Launch(UE_SOURCE_LOCATION, [] {}, Prereq) };
			verify(!Task.Wait(FTimespan::FromMilliseconds(10)));

			Event.Trigger();
			Task.Wait();
		}

		{	// compilation test of an iterable collection as prerequisites
			TArray<FTask> Prereqs{ Launch(UE_SOURCE_LOCATION, [] {}), FTaskEvent{ UE_SOURCE_LOCATION } };

			FTask Task{ Launch(UE_SOURCE_LOCATION, [] {}, Prereqs) };
			((FTaskEvent&)Prereqs[1]).Trigger();
			Task.Wait();
		}

		{	// compilation test of an initializer list as prerequisites
			auto Prereqs = { Launch(UE_SOURCE_LOCATION, [] {}), Launch(UE_SOURCE_LOCATION, [] {}) };
			Launch(UE_SOURCE_LOCATION, [] {}, Prereqs).Wait();
			// the following line doesn't compile because the compiler can't deduce the initializer list type
			//Launch(UE_SOURCE_LOCATION, [] {}, { Launch(UE_SOURCE_LOCATION, [] {}), Launch(UE_SOURCE_LOCATION, [] {}) }).Wait();
		}

		{	// a task is not executed until all its prerequisites (FTask and FTaskEvent instances) are completed
			FTaskEvent Prereq1{ UE_SOURCE_LOCATION };
			FTaskEvent Event{ UE_SOURCE_LOCATION };
			FTask Prereq2{ Launch(UE_SOURCE_LOCATION, [&Event] { Event.Wait(); }) };

			TTask<void> Task{ Launch(UE_SOURCE_LOCATION, [] {}, Prerequisites(Prereq1, Prereq2)) };
			verify(!Task.Wait(FTimespan::FromMilliseconds(10)));

			Prereq1.Trigger();
			verify(!Task.Wait(FTimespan::FromMilliseconds(10)));

			Event.Trigger();
			Task.Wait();
		}

		{	// a task is not executed until all its prerequisites (FTask and FTaskEvent instances) are completed. with explicit task priority
			FTaskEvent Prereq1{ UE_SOURCE_LOCATION };
			FTaskEvent Event{ UE_SOURCE_LOCATION };
			FTask Prereq2{ Launch(UE_SOURCE_LOCATION, [&Event] { Event.Wait(); }) };
			TArray<TTask<void>> Prereqs{ Prereq1, Prereq2 }; // to check if a random iterable container works as a prerequisite collection

			TTask<void> Task{ Launch(UE_SOURCE_LOCATION, [] {}, Prereqs, ETaskPriority::Normal) };
			verify(!Task.Wait(FTimespan::FromMilliseconds(10)));

			Prereq1.Trigger();
			verify(!Task.Wait(FTimespan::FromMilliseconds(10)));

			Event.Trigger();
			Task.Wait();
		}

		{	// a piped task blocked by a prerequisites doesn't block the pipe
			FPipe Pipe{ UE_SOURCE_LOCATION };
			FTaskEvent Prereq{ UE_SOURCE_LOCATION };

			FTask Task1{ Pipe.Launch(UE_SOURCE_LOCATION, [] {}, Prereq) };
			verify(!Task1.Wait(FTimespan::FromMilliseconds(10)));

			FTask Task2{ Pipe.Launch(UE_SOURCE_LOCATION, [] {}) };
			Task2.Wait();

			Prereq.Trigger();
			Task1.Wait();
		}

		{	// a piped task with multiple prerequisites
			FPipe Pipe{ UE_SOURCE_LOCATION };
			FTaskEvent Prereq1{ UE_SOURCE_LOCATION };
			FTaskEvent Event{ UE_SOURCE_LOCATION };
			FTask Prereq2{ Launch(UE_SOURCE_LOCATION, [&Event] { Event.Wait(); }) };

			FTask Task{ Pipe.Launch(UE_SOURCE_LOCATION, [] {}, Prerequisites(Prereq1, Prereq2)) };
			verify(!Task.Wait(FTimespan::FromMilliseconds(10)));

			Prereq1.Trigger();
			verify(!Task.Wait(FTimespan::FromMilliseconds(10)));
			Event.Trigger();
			Task.Wait();
		}

		{	// a piped task with multiple prerequisites. with explicit task priority
			FPipe Pipe{ UE_SOURCE_LOCATION };
			FTaskEvent Prereq1{ UE_SOURCE_LOCATION };
			FTaskEvent Event{ UE_SOURCE_LOCATION };
			FTask Prereq2{ Launch(UE_SOURCE_LOCATION, [&Event] { Event.Wait(); }) };

			FTask Task{ Pipe.Launch(UE_SOURCE_LOCATION, [] {}, Prerequisites(Prereq1, Prereq2), ETaskPriority::Normal) };
			verify(!Task.Wait(FTimespan::FromMilliseconds(10)));

			Prereq1.Trigger();
			verify(!Task.Wait(FTimespan::FromMilliseconds(10)));
			Event.Trigger();
			Task.Wait();
		}

		UE_BENCHMARK(5, DependenciesPerfTest<200, 10, 1000>);

		return true;
	}

	template<int NumTasks>
	void TestPerfBasic()
	{
		TArray<FTask> Tasks;
		Tasks.Reserve(NumTasks);

		for (int32 TaskIndex = 0; TaskIndex < NumTasks; ++TaskIndex)
		{
			Tasks.Emplace(Launch(UE_SOURCE_LOCATION, [] {}));
		}

		Wait(Tasks);
	}

	template<int32 NumTasks, int32 BatchSize>
	void TestPerfBatch()
	{
		static_assert(NumTasks % BatchSize == 0, "`NumTasks` must be divisible by `BatchSize`");
		constexpr int32 NumBatches = NumTasks / BatchSize;

		TArray<FTask> Batches;
		Batches.Reserve(NumBatches);
		TArray<FTask> Tasks;
		Tasks.AddDefaulted(NumTasks);

		for (int32 BatchIndex = 0; BatchIndex < NumBatches; ++BatchIndex)
		{
			Batches.Add(Launch(UE_SOURCE_LOCATION,
				[&Tasks, BatchIndex]
				{
					for (int32 TaskIndex = 0; TaskIndex < BatchSize; ++TaskIndex)
					{
						Tasks[BatchIndex * BatchSize + TaskIndex] = Launch(UE_SOURCE_LOCATION, [] {});
					}
				}
			));
		}

		Wait(Batches);
		Wait(Tasks);
	}

	template<int32 NumTasks, int32 BatchSize>
	void TestPerfBatchOptimised()
	{
		static_assert(NumTasks % BatchSize == 0, "`NumTasks` must be divisible by `BatchSize`");
		constexpr int32 NumBatches = NumTasks / BatchSize;

		FTaskEvent SpawnSignal{ UE_SOURCE_LOCATION };
		TArray<FTask> AllDone;

		for (int32 BatchIndex = 0; BatchIndex < NumBatches; ++BatchIndex)
		{
			AllDone.Add(Launch(UE_SOURCE_LOCATION,
				[]
				{
					FTaskEvent RunSignal{ UE_SOURCE_LOCATION };
					for (int32 TaskIndex = 0; TaskIndex < BatchSize; ++TaskIndex)
					{
						//AddNested(Launch(UE_SOURCE_LOCATION, [] {}, RunSignal));
					}
					RunSignal.Trigger();
				},
				SpawnSignal
			));
		}

		SpawnSignal.Trigger();
		Wait(AllDone);
	}

	template<int NumTasks>
	void TestLatency()
	{
		for (uint32 TaskIndex = 0; TaskIndex != NumTasks; ++TaskIndex)
		{
			Launch(UE_SOURCE_LOCATION, [] {}).Wait();
		}
	}

	template<uint32 NumTasks>
	void TestFGraphEventPerf()
	{
		FTaskEvent Prereq{ UE_SOURCE_LOCATION };
		std::atomic<uint32> CompletedTasks{ 0 };

		TArray<FTask> Tasks;
		for (int i = 0; i != NumTasks; ++i)
		{
			Tasks.Add(Launch(UE_SOURCE_LOCATION,
				[&Prereq, &CompletedTasks]()
				{
					Prereq.Wait();
					++CompletedTasks;
				}
			));
		}

		Prereq.Trigger();
		Wait(Tasks);

		check(CompletedTasks == NumTasks);
	}

	template<int NumTasks>
	void TestSpawning()
	{
		{
			TArray<FTask> Tasks;
			Tasks.Reserve(NumTasks);
			//double StartTime = FPlatformTime::Seconds();
			for (uint32 TaskNo = 0; TaskNo != NumTasks; ++TaskNo)
			{
				Tasks.Add(Launch(UE_SOURCE_LOCATION, [] {}));
			}

			//double Duration = FPlatformTime::Seconds() - StartTime;
			//UE_LOG(LogTemp, Display, TEXT("Spawning %d empty trackable tasks took %f secs"), NumTasks, Duration);

			Wait(Tasks);
		}
		{
			double StartTime = FPlatformTime::Seconds();
			for (uint32 TaskNo = 0; TaskNo != NumTasks; ++TaskNo)
			{
				Launch(UE_SOURCE_LOCATION, [] {});
			}

			//double Duration = FPlatformTime::Seconds() - StartTime;
			//UE_LOG(LogTemp, Display, TEXT("Spawning %d empty non-trackable tasks took %f secs"), NumTasks, Duration);
		}
	}

	template<int NumTasks>
	void TestBatchSpawning()
	{
		//double StartTime = FPlatformTime::Seconds();
		FTaskEvent Prereq{ UE_SOURCE_LOCATION };
		TArray<FTask> Tasks;
		Tasks.Reserve(NumTasks);
		for (uint32 TaskNo = 0; TaskNo != NumTasks; ++TaskNo)
		{
			Tasks.Add(Launch(UE_SOURCE_LOCATION, [] {}, Prereq));
		}

		//double SpawnedTime = FPlatformTime::Seconds();
		Prereq.Trigger();

		//double EndTime = FPlatformTime::Seconds();
		//UE_LOG(LogTemp, Display, TEXT("Spawning %d empty non-trackable tasks took %f secs total, %f secs spawning and %f secs dispatching"), NumTasks, EndTime - StartTime, SpawnedTime - StartTime, EndTime - SpawnedTime);

		Wait(Tasks);
	}

	template<int64 NumBatches, int64 NumTasksPerBatch>
	void TestWorkStealing()
	{
		TArray<FTask> Batches;
		Batches.Reserve(NumBatches);

		TArray<FTask> Tasks[NumBatches];
		for (int32 BatchIndex = 0; BatchIndex != NumBatches; ++BatchIndex)
		{
			Tasks[BatchIndex].Reserve(NumBatches * NumTasksPerBatch);
			Batches.Add(Launch(UE_SOURCE_LOCATION,
				[&Tasks, BatchIndex]()
				{
					for (int32 TaskIndex = 0; TaskIndex < NumTasksPerBatch; ++TaskIndex)
					{
						Tasks[BatchIndex].Add(Launch(UE_SOURCE_LOCATION, [] {}));
					}
				}
			));
		}

		Wait(Batches);
		for (int32 BatchIndex = 0; BatchIndex != NumBatches; ++BatchIndex)
		{
			Wait(Tasks[BatchIndex]);
		}
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTasksPerfTest, "System.Core.Async.TaskGraph.PerfTest", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter);

	bool FTasksPerfTest::RunTest(const FString& Parameters)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TaskGraphTests_PerfTest);

		//UE_BENCHMARK(1, TestSpawning<100000>);
		//return true;

		UE_BENCHMARK(5, TestPerfBasic<100000>);
		UE_BENCHMARK(5, TestPerfBatch<100000, 100>);
		UE_BENCHMARK(5, TestPerfBatchOptimised<100000, 100>);
		UE_BENCHMARK(5, TestLatency<10000>);
		//UE_BENCHMARK(5, TestFGraphEventPerf<100000>); // stack overflow
		UE_BENCHMARK(5, TestWorkStealing<100, 1000>);
		UE_BENCHMARK(5, TestSpawning<100000>);
		UE_BENCHMARK(5, TestBatchSpawning<100000>);

		return true;
	}
}}

#endif // WITH_DEV_AUTOMATION_TESTS
