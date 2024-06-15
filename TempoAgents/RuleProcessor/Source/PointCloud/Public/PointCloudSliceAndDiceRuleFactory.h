// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "PointCloudSliceAndDiceRule.h"

struct FSlateBrush;
class UPointCloudSliceAndDiceRuleSet;

/** This is a generic base class for objects that create instances of Slice and Dice Rules */
class FSliceAndDiceRuleFactory
{
public:
	virtual ~FSliceAndDiceRuleFactory() = default;

	/**
	* Return the name for this rule
	*
	* @return The name of the rule created by this factory to display to the user in menus etc
	*/
	virtual FString Name() const = 0;

	/**
	* A description of the rule 
	*
	* @return A human readable description of the rule
	*/
	virtual FString Description() const = 0;

	/**
	* Create an instance of this rule
	*
	* @return a pointer to a new instance of the rule. The caller will take ownership 
	*/
	UPointCloudRule* CreateRule(UPointCloudSliceAndDiceRuleSet* Parent);

	/** Return an Icon to represent this rule 
	*
	* @return a pointer to an Icon that represents this Rule
	*/
	virtual FSlateBrush* GetIcon() const { return nullptr; }

	/** Return the type of this rule */
	virtual UPointCloudRule::RuleType GetType() const = 0;
	
protected:
	/**
	* Create an instance of this rule
	*
	* @return a pointer to a new instance of the rule. The caller will take ownership
	*/
	virtual UPointCloudRule* Create(UObject* Parent) = 0;
};

 