// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PointCloudSliceAndDiceShared.h"
#include "PointCloudSliceAndDiceRuleData.h"
#include "PointCloudSliceAndDiceReport.h"
#include "PointCloudStats.h"

class UPointCloud;
class UPointCloudView;
class UPointCloudRule;
class USliceAndDiceManagedActors;
class AActor;

class FSliceAndDiceExecutionContext;
using FSliceAndDiceExecutionContextPtr = TSharedPtr<FSliceAndDiceExecutionContext>;

class FPointCloudRuleInstance;
using FPointCloudRuleInstancePtr = TSharedPtr<FPointCloudRuleInstance>;

class POINTCLOUD_API FPointCloudRuleInstance
{
public:
	virtual ~FPointCloudRuleInstance() {}

	/** Returns a unique rule instance type, used to check for same class properties */
	virtual uint32 GetInstanceType() { return 0; }

	/** Returns true if this can be executed on any thread */
	virtual bool CanBeExecutedOnAnyThread() const { return true; }

	/** Applies data overrides then executes this rule & children rules */
	virtual bool PreExecute(FSliceAndDiceExecutionContextPtr Context);

	/** Tidies up work done in this rule or in children's */
	virtual bool PostExecute(FSliceAndDiceExecutionContextPtr Context);

	/** Set the point cloud this rule instance should use 
	* @param InPointCloud The New Point Cloud to Use
	*/
	void SetPointCloud(UPointCloud* InPointCloud)
	{
		PointCloud = InPointCloud;
	}

	/** Set the view this rule instance should use
	* @param InView The view this instance should use. This should not be nullptr
	*/
	void SetView(UPointCloudView* InView)
	{
		if (InView == nullptr)
		{
			// Dont allow the View to be set to a null value
			return;
		}
		View = InView;
	}

	/** Force clear the view set on this Instance. View will be nullptr after this. This will also remove the view from any parent views */
	void ClearView();
	
	void SetParent(FPointCloudRuleInstancePtr InParent)
	{
		Parent = InParent;
	}

	void AddChild(FPointCloudRuleInstancePtr InChild)
	{
		Children.Add(InChild);
	}

	const UPointCloudRule* GetRule() const
	{
		return Rule;
	}

	void SetWorld(UWorld* InWorld)
	{
		if (DataPtr)
		{
			DataPtr->World = InWorld;
		}
	}

	virtual FString GetHash();
	FString GetParentHash();

	/** Add a pointer to a Point Cloud Stats Gathering Object. This can be used to record timing information for runs 
	* and statistics about how many actors, components, Ism, Niagara systems etc are created 
	* @param InStats A pointer to a valid stats gathering object to use
	*/
	void SetStats(FPointCloudStatsPtr InStats)
	{
		StatsPtr = InStats;
	}

	FPointCloudStatsPtr GetStats() const
	{
		return StatsPtr;
	}

	UWorld* GetWorld() const
	{
		return DataPtr ? DataPtr->World : nullptr;
	}

	UPointCloud* GetPointCloud() const
	{
		return PointCloud;
	}

	/** Returns array of generated actors (mappings) in this rule instance */
	const TArray<FSliceAndDiceActorMapping>& GetGeneratedActors() const
	{ 
		return NewActors; 
	}

	FPointCloudSliceAndDiceReportFramePtr GetReportFrame() const
	{
		return ReportFrame;
	}

	void SetReportingMode(EPointCloudReportMode InReportingMode)
	{
		ReportingMode = InReportingMode;
	}

	void SetReportFrame(FPointCloudSliceAndDiceReportFramePtr InFrame)
	{
		ReportFrame = InFrame;
	}

	FPointCloudRuleInstancePtr Parent;
	TArray<FPointCloudRuleInstancePtr> Children;

	/** Multithreading convenience methods to trigger post-execution */
	void ResetExecutingChildCount()
	{
		ExecutingChildCount = Children.Num();
	}

	bool EndChildExecution()
	{
		return (--ExecutingChildCount <= 0);
	}

	/** Duplicates this instance & its children recursively */
	FPointCloudRuleInstancePtr Duplicate(bool bAttachToParent) const;

	/** Returns true if this instance has been skipped by rule revision & hash verification */
	bool IsSkipped() const { return bIsSkipped; }

	/** Returns true if the execuction of children instances is to be skipped (can be because of local skip or other processes) */
	bool AreChildrenSkipped() const { return bAreChildrenSkipped; }

	/** Sets the ManagedActors this rule instances is associated to, used as an optimization. */
	void SetManagedActors(USliceAndDiceManagedActors* InManagedActors) { ManagedActors = InManagedActors; }

	/** Returns the associated managed actors to this rule instance */
	USliceAndDiceManagedActors* GetManagedActors() const { return ManagedActors; }

protected:
	/** Construct (from Compile) */
	explicit FPointCloudRuleInstance(const UPointCloudRule* InRule, FPointCloudRuleData* InData = nullptr)
		: Rule(InRule), DataPtr(InData)
	{}

	/** Duplicate (for execution time instance creation) */
	FPointCloudRuleInstance(const FPointCloudRuleInstance& InToCopy, FPointCloudRuleData* InData = nullptr);

	/** Creates a copy of this instance, excluding children */
	virtual FPointCloudRuleInstancePtr DuplicateInternal() const = 0;

	/** Return true if the rule should calculate reporting information */
	bool GenerateReporting() const;

	/** Return true if the rule should generate assets in the scene */
	bool GenerateAssets() const;

	/** Marks this instance to be skipped (will skip Pre-Execute & Post-Execute) */
	void SetIsSkipped(bool bInIsSkipped) { bIsSkipped = bInIsSkipped; }

	/** Marks this instance as not calling execution of its children (useful for skipped & some iterators) */
	void SetSkipChildren(bool bInSkipChildren) { bAreChildrenSkipped = bInSkipChildren; }

	/**
	* Executes the rule instance w.r.t its parent and its properties
	* @return Returns true if execution was successful
	*/
	virtual bool Execute() { return true; }
	virtual bool Execute(FSliceAndDiceExecutionContextPtr Context) { return Execute(); }

	/**
	* Executes the post-rule instance after all its children have been executed
	*/
	virtual bool PostExecuteInternal(FSliceAndDiceExecutionContextPtr Context);
	virtual bool PostExecute() { return true; }

	/**
	* Updates the data stored in this instance
	*/
	virtual void UpdateData();

	/**
	* Keeps track of a new actor(s)
	*/
	void NewActorAdded(AActor* InActor, UPointCloudView* InView);
	void NewActorsAdded(const TArray<AActor*>& InActors, UPointCloudView* InView);
	void NewActorsAdded(const TArray<AActor*>& InActors, const TArray<FActorInstanceHandle>& InActorHandles, UPointCloudView* InView);

	/** Gathers generated actors recursively & clears them */
	TArray<FSliceAndDiceActorMapping> ReturnAndClearGeneratedActors();

	/**
	* Returns the view associated to this rule instance;
	* Will create a view on the first call to this method.
	*/
	UPointCloudView* GetView();
	
	UPointCloud* PointCloud = nullptr;
	UPointCloudView* View = nullptr;
	const UPointCloudRule* Rule = nullptr;
	FPointCloudRuleData* DataPtr = nullptr;
	FPointCloudStatsPtr StatsPtr;
	FPointCloudSliceAndDiceReportFramePtr ReportFrame;
	EPointCloudReportMode ReportingMode = EPointCloudReportMode::Execute;

	bool bIsSkipped = false;
	bool bAreChildrenSkipped = false;
	USliceAndDiceManagedActors* ManagedActors = nullptr;

	/** New actor mappings generated (1 view -> N actors per entry) */
	TArray<FSliceAndDiceActorMapping> NewActors;

	/** Counter for multi-threaded execution */
	std::atomic<int32> ExecutingChildCount{ 0 };
};

template<class Derived>
class FPointCloudRuleInstanceCRTP : public FPointCloudRuleInstance
{
public:
	TSharedPtr<Derived> Duplicate(bool bAttachToParent) const
	{
		return StaticCastSharedPtr<Derived>(FPointCloudRuleInstance::Duplicate(bAttachToParent));
	}

protected:
	// Creation
	FPointCloudRuleInstanceCRTP(const UPointCloudRule* InRule, FPointCloudRuleData* InData)
		: FPointCloudRuleInstance(InRule, InData)
	{}

	// Duplication
	FPointCloudRuleInstanceCRTP(const FPointCloudRuleInstanceCRTP& InToCopy, FPointCloudRuleData* InData)
		: FPointCloudRuleInstance(InToCopy, InData)
	{}

	virtual FPointCloudRuleInstancePtr DuplicateInternal() const override
	{
		return MakeShareable(new Derived(static_cast<const Derived&>(*this)));
	}
};

template<class Derived, class DataType>
class FPointCloudRuleInstanceWithData : public FPointCloudRuleInstanceCRTP<Derived>
{
protected:
	FPointCloudRuleInstanceWithData(const UPointCloudRule* InRule, const DataType& InData)
		: FPointCloudRuleInstanceCRTP<Derived>(InRule, &Data), Data(InData)
	{
	}

	FPointCloudRuleInstanceWithData(const FPointCloudRuleInstanceWithData& InToCopy)
		: FPointCloudRuleInstanceCRTP<Derived>(InToCopy, &Data), Data(InToCopy.Data)
	{
	}

	DataType Data;
};
