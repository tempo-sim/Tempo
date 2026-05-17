// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensorsUtils.h"

void OptimizeShowFlagsForNoColor(FEngineShowFlags& ShowFlags)
{
	ShowFlags.SetPostProcessing(true);
	ShowFlags.SetPostProcessMaterial(true);
	ShowFlags.SetHighResScreenshotMask(false);
	ShowFlags.SetHMDDistortion(false);
	ShowFlags.SetStereoRendering(false);
	ShowFlags.SetLocalExposure(false);
	ShowFlags.SetTonemapper(false);
	ShowFlags.SetAntiAliasing(false);
	ShowFlags.SetTemporalAA(false);
	ShowFlags.SetAmbientCubemap(false);
	ShowFlags.SetEyeAdaptation(false);
	ShowFlags.SetLensFlares(false);
	ShowFlags.SetBloom(false);
	ShowFlags.SetGlobalIllumination(false);
	ShowFlags.SetVignette(false);
	ShowFlags.SetGrain(false);
	ShowFlags.SetAmbientOcclusion(false);
	ShowFlags.SetCameraImperfections(false);
	ShowFlags.SetLighting(false);
	ShowFlags.SetDirectLighting(false);
	ShowFlags.SetDirectionalLights(false);
	ShowFlags.SetPointLights(false);
	ShowFlags.SetRectLights(false);
	ShowFlags.SetColorGrading(false);
	ShowFlags.SetDepthOfField(false);
	ShowFlags.SetMotionBlur(false);
	ShowFlags.SetRefraction(false);
	ShowFlags.SetSceneColorFringe(false);
	ShowFlags.SetCameraInterpolation(false);
	ShowFlags.SetToneCurve(false);
	ShowFlags.SetSeparateTranslucency(false);
	ShowFlags.SetReflectionEnvironment(false);
	ShowFlags.SetDecals(false);
	// ShowFlags.SetHairStrands(false);
	ShowFlags.SetDiffuse(false);
	ShowFlags.SetSpecular(false);
	ShowFlags.SetScreenSpaceReflections(false);
	ShowFlags.SetLumenReflections(false);
	ShowFlags.SetContactShadows(false);
	ShowFlags.SetRayTracedDistanceFieldShadows(false);
	ShowFlags.SetCapsuleShadows(false);
	ShowFlags.SetSubsurfaceScattering(false);
	ShowFlags.SetVolumetricLightmap(false);
	ShowFlags.SetIndirectLightingCache(false);
	ShowFlags.SetTexturedLightProfiles(false);
	ShowFlags.SetLightFunctions(false);
	ShowFlags.SetDynamicShadows(false);
	ShowFlags.SetTranslucency(false);
	ShowFlags.SetDeferredLighting(false);
	ShowFlags.SetLightShafts(false);
	ShowFlags.SetAtmosphere(false);
	ShowFlags.SetCloud(false);
	ShowFlags.SetScreenSpaceAO(false);
	ShowFlags.SetDistanceFieldAO(false);
	ShowFlags.SetLumenGlobalIllumination(false);
	ShowFlags.SetVolumetricFog(false);
	ShowFlags.SetFog(false);
	ShowFlags.SetShaderPrint(false);
	ShowFlags.SetVirtualShadowMapPersistentData(false);

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	ShowFlags.SetLensDistortion(false);
	ShowFlags.SetMegaLights(false);
#endif
}

void ApplyPhotorealisticRenderSettings(FPostProcessSettings& OutPostProcess,
	FEngineShowFlags& OutShowFlags, bool& OutUseRayTracingIfEnabled)
{
	// Auto exposure: AEM_Histogram samples the (PPM-replaced, for the camera; raw scene, for the
	// lidar) scene color histogram to compute exposure. The aggressive speed-up/down keeps the
	// exposure responsive when the scene brightness shifts rapidly between captures.
	OutPostProcess.bOverride_AutoExposureMethod = true;
	OutPostProcess.AutoExposureMethod = AEM_Histogram;
	OutPostProcess.bOverride_AutoExposureSpeedUp = true;
	OutPostProcess.AutoExposureSpeedUp = 20.0;
	OutPostProcess.bOverride_AutoExposureSpeedDown = true;
	OutPostProcess.AutoExposureSpeedDown = 20.0;
	OutPostProcess.bOverride_AutoExposureLowPercent = true;
	OutPostProcess.AutoExposureLowPercent = 75.0;
	OutPostProcess.bOverride_AutoExposureHighPercent = true;
	OutPostProcess.AutoExposureHighPercent = 85.0;

	// Lumen
	OutPostProcess.bOverride_DynamicGlobalIlluminationMethod = true;
	OutPostProcess.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::Lumen;
	OutPostProcess.bOverride_ReflectionMethod = true;
	OutPostProcess.ReflectionMethod = EReflectionMethod::Lumen;

	// Lumen final-gather denoiser leans on temporal history; in dim scenes the signal-to-noise
	// ratio is poor and history dominates, which manifests as ghost trails behind slow movers.
	// Bumping FinalGatherQuality reduces input noise and bumping the update speed shortens the
	// effective history window so trails fade faster. Reflection quality has the same effect for
	// specular ghosts.
	OutPostProcess.bOverride_LumenFinalGatherQuality = true;
	OutPostProcess.LumenFinalGatherQuality = 2.0f;
	OutPostProcess.bOverride_LumenFinalGatherLightingUpdateSpeed = true;
	OutPostProcess.LumenFinalGatherLightingUpdateSpeed = 4.0f;
	OutPostProcess.bOverride_LumenReflectionQuality = true;
	OutPostProcess.LumenReflectionQuality = 2.0f;

	// Megalights
	OutPostProcess.bOverride_bMegaLights = true;
	OutPostProcess.bMegaLights = true;

	OutUseRayTracingIfEnabled = true;

	OutShowFlags.SetMotionBlur(false);
	OutShowFlags.SetAntiAliasing(true);
	OutShowFlags.SetTemporalAA(true);
	OutShowFlags.SetEyeAdaptation(true);
	OutShowFlags.SetLocalExposure(true);
	OutShowFlags.SetLensFlares(true);
	OutShowFlags.SetBloom(true);
	OutShowFlags.SetColorGrading(true);
	OutShowFlags.SetVignette(true);
	OutShowFlags.SetDepthOfField(true);
	OutShowFlags.SetGlobalIllumination(true);
	OutShowFlags.SetScreenSpaceReflections(true);
	OutShowFlags.SetReflectionEnvironment(true);
	OutShowFlags.SetAmbientOcclusion(true);
	OutShowFlags.SetScreenSpaceAO(true);
	OutShowFlags.SetDistanceFieldAO(true);
	OutShowFlags.SetVolumetricFog(true);
	OutShowFlags.SetTonemapper(true);
	OutShowFlags.SetScreenPercentage(true);
}
