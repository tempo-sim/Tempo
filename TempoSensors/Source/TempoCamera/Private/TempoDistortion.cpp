// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoDistortion.h"

#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "OpenCVLensDistortionBlueprintLibrary.h"
#include "TextureResource.h"

static void DistortPointInternal(double x, double y, 
                                 double k1, double k2, double k3, 
                                 double p1, double p2, 
                                 double& out_x, double& out_y)
{
    double r2 = x*x + y*y;
    double r4 = r2*r2;
    double r6 = r4*r2;

    // Radial Distortion
    double cDist = 1.0 + k1*r2 + k2*r4 + k3*r6;
	
    double a1 = 2.0 * x * y;
    double a2 = r2 + 2.0 * x * x;
    double a3 = r2 + 2.0 * y * y;
    out_x = x * cDist + p1 * a1 + p2 * a2;
    out_y = y * cDist + p1 * a3 + p2 * a1;
}

ATempoDistortion::ATempoDistortion()
{
    PrimaryActorTick.bCanEverTick = true;
	
    LensParameters.K1 = 0.0f;
    LensParameters.K2 = 0.0f;
    LensParameters.K3 = 0.0f;
    LensParameters.P1 = 0.0f;
    LensParameters.P2 = 0.0f;
	
    LensParameters.F = FVector2D(0.5f, 0.5f);
    LensParameters.C = FVector2D(0.5f, 0.5f);
    LensParameters.bUseFisheyeModel = false;
    ManualResolution = FIntPoint(1920, 1080);
}

void ATempoDistortion::BeginPlay()
{
    Super::BeginPlay();
    UpdateDistortionMap();
}

void ATempoDistortion::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    
    if (bUpdateEveryFrame)
    {
        UpdateDistortionMap();
    }
}

UTexture2D* ATempoDistortion::GenerateTrueDistortionMap(const FIntPoint& InSize)
{
    UTexture2D* Result = UTexture2D::CreateTransient(InSize.X, InSize.Y, PF_G16R16F);
    Result->CompressionSettings = TC_HDR;
    Result->Filter = TF_Bilinear;
    Result->SRGB = 0;
    
    // Set clamping to avoid edge bleeding/streaking
    Result->AddressX = TA_Clamp;
    Result->AddressY = TA_Clamp;

    FTexture2DMipMap& Mip = Result->GetPlatformData()->Mips[0];
    uint16* MipData = reinterpret_cast<uint16*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	
    double fx = LensParameters.F.X * InSize.X;
    double fy = fx;
	
    double cx = LensParameters.C.X * InSize.X;
    double cy = LensParameters.C.Y * InSize.Y;

    double k1 = LensParameters.K1;
    double k2 = LensParameters.K2;
    double k3 = LensParameters.K3;
    double p1 = LensParameters.P1;
    double p2 = LensParameters.P2;
	
    const int max_iterations = 10; 
    const double epsilon = 1e-6;

    for (int v = 0; v < InSize.Y; ++v)
    {
        uint16* Row = &MipData[v * InSize.X * 2];
        
        for (int u = 0; u < InSize.X; ++u)
        {
            double x_target = (u - cx) / fx;
            double y_target = (v - cy) / fy;

            // Initial Guess for Undistorted Point (Xu, Yu)
            double x = x_target;
            double y = y_target;
        	
            // We want to find (x,y) such that Distort(x,y) == (x_target, y_target)
            for (int j = 0; j < max_iterations; ++j)
            {
                double r2 = x*x + y*y;
                double r4 = r2*r2;
                double r6 = r4*r2;
            	
                double x_dist, y_dist;
                DistortPointInternal(x, y, k1, k2, k3, p1, p2, x_dist, y_dist);

                // Calculate Error
                double err_x = x_target - x_dist;
                double err_y = y_target - y_dist;
            	
                if ((err_x*err_x + err_y*err_y) < epsilon)
                    break;
            	
                double derivative = 1.0 + 3.0*k1*r2 + 5.0*k2*r4 + 7.0*k3*r6;
            	
                if (FMath::Abs(derivative) < 0.0001) derivative = 1.0;

                x += err_x / derivative;
                y += err_y / derivative;
            }
        	
            // We take the undistorted point and map it back to UVs
            float final_u = (float)(x * fx + cx) / (float)InSize.X;
            float final_v = (float)(y * fy + cy) / (float)InSize.Y;
        	
            Row[u * 2 + 0] = FFloat16(final_u).Encoded;
            Row[u * 2 + 1] = FFloat16(final_v).Encoded;
        }
    }
    
    Mip.BulkData.Unlock();

#ifdef UpdateResource
#undef UpdateResource
#endif
    
    Result->UpdateResource();
    return Result;
}

void ATempoDistortion::UpdateDistortionMap()
{
    if (!OutputRenderTarget) return;

    FIntPoint TargetSize = ManualResolution;
    if (bMatchViewportResolution && GetWorld() && GetWorld()->GetGameViewport())
    {
       FVector2D ViewportSize;
       GetWorld()->GetGameViewport()->GetViewportSize(ViewportSize);
        
       if (ViewportSize.X > 0 && ViewportSize.Y > 0)
       {
          TargetSize = FIntPoint(ViewportSize.X, ViewportSize.Y);
       }
    }
    
    if (OutputRenderTarget->SizeX != TargetSize.X || OutputRenderTarget->SizeY != TargetSize.Y)
    {
       OutputRenderTarget->ResizeTarget(TargetSize.X, TargetSize.Y);
    }
    
    UTexture2D* DisplacementMap = GenerateTrueDistortionMap(TargetSize);
    
    if (DisplacementMap)
    {
       FOpenCVLensDistortionParameters::DrawDisplacementMapToRenderTarget(
          GetWorld(), 
          OutputRenderTarget, 
          DisplacementMap
       );
    }
}