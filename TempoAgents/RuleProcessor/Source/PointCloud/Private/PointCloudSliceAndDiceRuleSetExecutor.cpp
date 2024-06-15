// Copyright Epic Games, Inc. All Rights Reserved.

#include "PointCloudSliceAndDiceRuleSetExecutor.h"
#include "PointCloudSliceAndDiceContext.h"
#include "PointCloudSliceAndDiceRule.h"

#include "Async/TaskGraphInterfaces.h"

static TAutoConsoleVariable<int32> CVarRuleSetExecutorMultithreaded(
	TEXT("t.RuleProcessor.RuleSetExecutorMultithreaded"),
	0,
	TEXT("If non-zero, will run rule set in multithreaded mode."));

FPointCloudSliceAndDiceRuleSetExecutor::FPointCloudSliceAndDiceRuleSetExecutor(FSliceAndDiceContext& InContext)
	: Context(InContext)
{

}

bool FPointCloudSliceAndDiceRuleSetExecutor::Execute()
{
	PrepareWorkloads();
	return ExecuteWorkloads();
}

void FPointCloudSliceAndDiceRuleSetExecutor::PrepareWorkloads()
{
	RuleInstances = Context.GetAllRootInstances();

	// TODO: optimize/merge rule instances
	// TODO: bin jobs with no dependencies in the same workload element
	// TODO: serialize single-dependency chains in the same workload element
	// TODO: revisit if the workloads should consider thread affinity restrictions
	//		since it would simplify the ExecuteWorkloads call
}

DECLARE_STATS_GROUP(TEXT("RuleProcessor"), STATGROUP_RuleProcessor, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("Execution Time"), STAT_RuleProcessorExecutionTime, STATGROUP_RuleProcessor)

bool FPointCloudSliceAndDiceRuleSetExecutor::ExecuteWorkloads()
{
	const bool bSaveAndUnload = true;
	FSliceAndDiceExecutionContextPtr ExecutionContext = MakeShareable(new FSliceAndDiceExecutionContext(Context, bSaveAndUnload));

	if (CVarRuleSetExecutorMultithreaded.GetValueOnAnyThread() != 0)
	{
		// Known issue: with the task graph usage like we do, we do not wait until the jobs are done
		// because 1) we don't know what to wait for until we're executing
		// and 2) using an event here prevents the task graph from executing
		// and 3) we have no guarantees that even if we were to dispatch a waiting task that it would
		//  be executed first
		// However, the task graph will be "emptied" later in the frame in any case
		QueueRuleInstances(nullptr, RuleInstances, ExecutionContext);
	}
	else // single threaded
	{
		for (FPointCloudRuleInstancePtr& RuleInstance : RuleInstances)
		{
			SliceAndDiceExecution::SingleThreadedRuleInstanceExecute(RuleInstance, ExecutionContext);
		}
	}

	return true;
}

void FPointCloudSliceAndDiceRuleSetExecutor::QueueRuleInstances(FPointCloudRuleInstancePtr InParentInstance, const TArray<FPointCloudRuleInstancePtr>& InChildInstances, FSliceAndDiceExecutionContextPtr ExecutionContext)
{
	// Setup execution count to manage post-execute properly
	if (InParentInstance)
	{
		InParentInstance->ResetExecutingChildCount();
	}

	// Dispatch the pre-execute; there's no need to setup pre-reqs here as the parent job has executed.
	// NOTE: not true if we move instances in the hierarchy at compilation
	for (FPointCloudRuleInstancePtr Child : InChildInstances)
	{
		FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
			FSimpleDelegateGraphTask::FDelegate::CreateLambda([InParentInstance, Child, ExecutionContext, this]() {
				Child->PreExecute(ExecutionContext);

				if(!Child->IsSkipped() && !Child->AreChildrenSkipped())
				{
					QueueRuleInstances(Child, Child->Children, ExecutionContext);
				}
				else if(InParentInstance)
				{
					NotifyParentInstanceThatChildJobIsDone(InParentInstance, ExecutionContext);
				}
			}),
			GET_STATID(STAT_RuleProcessorExecutionTime),
			nullptr,
			Child->CanBeExecutedOnAnyThread() ? ENamedThreads::AnyThread : ENamedThreads::GameThread
		);
	}

	// Queue post-execute if this is a leaf-node
	if (InParentInstance && InChildInstances.Num() == 0)
	{
		NotifyParentInstanceThatChildJobIsDone(InParentInstance, ExecutionContext);
	}
}

void FPointCloudSliceAndDiceRuleSetExecutor::NotifyParentInstanceThatChildJobIsDone(FPointCloudRuleInstancePtr InInstance, FSliceAndDiceExecutionContextPtr ExecutionContext)
{
	if (InInstance && InInstance->EndChildExecution())
	{
		FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
			FSimpleDelegateGraphTask::FDelegate::CreateLambda([InInstance, ExecutionContext, this]() {
				InInstance->PostExecute(ExecutionContext);
				NotifyParentInstanceThatChildJobIsDone(InInstance->Parent, ExecutionContext);
				}),
			GET_STATID(STAT_RuleProcessorExecutionTime),
			nullptr,
			ENamedThreads::GameThread // Note: loading related calls will happen here, so always on game thread
		);
	}
}