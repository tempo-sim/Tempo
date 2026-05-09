// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoBrightnessMeter.h"

ATempoBrightnessMeter::ATempoBrightnessMeter()
{
	BrightnessMeterComponent = CreateDefaultSubobject<UTempoBrightnessMeterComponent>(TEXT("BrightnessMeter"));
	RootComponent = BrightnessMeterComponent;
}
