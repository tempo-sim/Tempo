// Copyright Epic Games, Inc. All Rights Reserved.


#include "MassTrafficFieldActor.h"

AMassTrafficFieldActor::AMassTrafficFieldActor()
{
 	FieldComponent = CreateDefaultSubobject<UMassTrafficFieldComponent>(TEXT("FieldComponent"));
	RootComponent = FieldComponent;
}
