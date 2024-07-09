// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PointCloudSliceAndDiceRuleInstance.h"
#include "PointCloudSliceAndDiceExecutionContext.h"

class FSliceAndDiceContext;

class FPointCloudSliceAndDiceRuleSetExecutor
{
public:
	FPointCloudSliceAndDiceRuleSetExecutor(FSliceAndDiceContext& InContext);
	bool Execute();

private:
	void PrepareWorkloads();
	bool ExecuteWorkloads();

	void QueueRuleInstances(FPointCloudRuleInstancePtr InParentInstance, const TArray<FPointCloudRuleInstancePtr>& InChildInstances, FSliceAndDiceExecutionContextPtr Context);
	void NotifyParentInstanceThatChildJobIsDone(FPointCloudRuleInstancePtr InInstance, FSliceAndDiceExecutionContextPtr Context);

private:
	FSliceAndDiceContext& Context;
	TArray<FPointCloudRuleInstancePtr> RuleInstances;
};