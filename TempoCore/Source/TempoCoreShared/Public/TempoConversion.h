// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

enum EUnitConversion
{
    UC_NONE,
    CM2M,
    M2CM,
    Rad2Deg,
    Deg2Rad
};

enum EHandednessConversion
{
    HC_NONE,
    R2L,
    L2R
};

template <EUnitConversion>
struct ConversionFactor;

template <> struct ConversionFactor<CM2M> { static constexpr double value = 0.01; };
template <> struct ConversionFactor<M2CM> { static constexpr double value = 100.0; };
template <> struct ConversionFactor<Rad2Deg> { static constexpr double value = 180.0 / M_PI; };
template <> struct ConversionFactor<Deg2Rad> { static constexpr double value = M_PI / 180.0; };

template <EUnitConversion UnitConversion = UC_NONE, EHandednessConversion HandednessConversion = HC_NONE>
struct QuantityConverter
{
    static FVector2D Convert(const FVector2D& In)
    {
        return QuantityConverter<UC_NONE, HandednessConversion>::Convert(QuantityConverter<UnitConversion, HC_NONE>::Convert(In));
    }

    static FVector Convert(const FVector& In)
    {
        return QuantityConverter<UC_NONE, HandednessConversion>::Convert(QuantityConverter<UnitConversion, HC_NONE>::Convert(In));
    }
};

template<>
struct QuantityConverter<UC_NONE, R2L>
{
    static FVector2D Convert(const FVector2D& In)
    {
        return FVector2D(In.X, -In.Y);
    }

    static FVector Convert(const FVector& In)
    {
        return FVector(In.X, -In.Y, In.Z);
    }

    static FQuat Convert(const FQuat& In)
    {
        return FQuat(-In.X, In.Y, -In.Z, In.W);
    }

    static FRotator Convert(const FRotator& In)
    {
        return FRotator(Convert(In.Quaternion()));
    }
};

template<>
struct QuantityConverter<UC_NONE, L2R>
{
    static FVector2D Convert(const FVector2D& In)
    {
        return FVector2D(In.X, -In.Y);
    }

    static FVector Convert(const FVector& In)
    {
        return FVector(In.X, -In.Y, In.Z);
    }

    static FQuat Convert(const FQuat& In)
    {
        return FQuat(-In.X, In.Y, -In.Z, In.W);
    }

    static FRotator Convert(const FRotator& In)
    {
        return FRotator(Convert(In.Quaternion()));
    }
};

template <EUnitConversion UnitConversion>
struct QuantityConverter<UnitConversion, HC_NONE>
{
    template <typename T>
    static T Convert(const T& In)
    {
        return ConversionFactor<UnitConversion>::value * In;
    }
};
