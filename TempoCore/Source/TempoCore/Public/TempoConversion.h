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
template <> struct ConversionFactor<Rad2Deg> { static constexpr double value = 180.0 / UE_PI; };
template <> struct ConversionFactor<Deg2Rad> { static constexpr double value = UE_PI / 180.0; };

template <EUnitConversion UnitConversion = UC_NONE, EHandednessConversion HandednessConversion = HC_NONE>
struct QuantityConverter
{
    template <typename T>
    static T Convert(const T& In)
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
        // The vector formed by a quaternion's X, Y, and Z components is aligned with the axis of rotation.
        // The angle of rotation is proportional to the cosine of the W component.
        // Converting between handedness involves two steps:
        // - Converting the handedness of the axis of rotation (negate Y)
        // - Rotating in the opposite direction (negate X, Y, and Z. W doesn't change because cosine is symmetric)
        return FQuat(-In.X, In.Y, -In.Z, In.W);
    }

    static FRotator Convert(const FRotator& In)
    {
        // Unreal uses a unique rotation convention:
        // Intrinsic rotations (Yaw, then Pitch, then Roll) about a left-handed (forward, right, up) coordinate system.
        // BUT:
        // - Yaw is left-handed
        // - Pitch is *right-handed*
        // - Roll is *right-handed*
        // So to convert to/from a true right handed (forward, left, up) coordinate system, with the same ordering:
        // - Negate Yaw (due to handedness)
        // - Negate Pitch (due to flipped axis)
        // - Leave Roll (no change needed)
        return FRotator(-In.Pitch, -In.Yaw, In.Roll);
    }
};

template<>
struct QuantityConverter<UC_NONE, L2R>
{
    // Handedness conversions are all symmetric

    static FVector2D Convert(const FVector2D& In)
    {
        return QuantityConverter<UC_NONE, R2L>::Convert(In);
    }

    static FVector Convert(const FVector& In)
    {
        return QuantityConverter<UC_NONE, R2L>::Convert(In);
    }

    static FQuat Convert(const FQuat& In)
    {
        return QuantityConverter<UC_NONE, R2L>::Convert(In);
    }

    static FRotator Convert(const FRotator& In)
    {
        return QuantityConverter<UC_NONE, R2L>::Convert(In);
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
