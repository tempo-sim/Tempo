// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoMapQueryServiceSubsystem.h"

#include "TempoMapQuery/MapQueries.grpc.pb.h"
#include "TempoMapQuery.h"

#include "TempoConversion.h"

#include "MassTrafficSubsystem.h"
#include "ZoneGraphData.h"
#include "ZoneGraphSubsystem.h"

#include "EngineUtils.h"

using MapQueryService = TempoMapQuery::MapQueryService;
using MapQueryAsyncService = TempoMapQuery::MapQueryService::AsyncService;
using LaneDataRequest = TempoMapQuery::LaneDataRequest;
using LaneDataResponse = TempoMapQuery::LaneDataResponse;
using LaneAccessibilityRequest = TempoMapQuery::LaneAccessibilityRequest;
using LaneAccessibilityResponse = TempoMapQuery::LaneAccessibilityResponse;

void UTempoMapQueryServiceSubsystem::RegisterScriptingServices(FTempoScriptingServer& ScriptingServer)
{
	ScriptingServer.RegisterService<MapQueryService>(
		SimpleRequestHandler(&MapQueryAsyncService::RequestGetLanes, &UTempoMapQueryServiceSubsystem::GetLaneData),
		SimpleRequestHandler(&MapQueryAsyncService::RequestGetLaneAccessibility, &UTempoMapQueryServiceSubsystem::GetLaneAccessibility),
		StreamingRequestHandler(&MapQueryAsyncService::RequestStreamLaneAccessibility, &UTempoMapQueryServiceSubsystem::StreamLaneAccessibility)
		);
}

void UTempoMapQueryServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FTempoScriptingServer::Get().ActivateService<MapQueryService>(this);
}

void UTempoMapQueryServiceSubsystem::Deinitialize()
{
	Super::Deinitialize();

	FTempoScriptingServer::Get().DeactivateService<MapQueryService>();
}

TempoMapQuery::LaneRelationship LaneRelationshipFromLinkType(const EZoneLaneLinkType& LinkType)
{
	switch (LinkType)
	{
	case EZoneLaneLinkType::Incoming:
		{
			return TempoMapQuery::LaneRelationship::PREDECESSOR;
		}
	case EZoneLaneLinkType::Outgoing:
		{
			return TempoMapQuery::LaneRelationship::SUCCESSOR;
		}
	case EZoneLaneLinkType::Adjacent:
		{
			return TempoMapQuery::LaneRelationship::NEIGHBOR;
		}
	default:
		{
			UE_LOG(LogTempoMapQuery, Error, TEXT("Unhandled lane link type"));
			return TempoMapQuery::LaneRelationship::LR_UNKNOWN;
		}
	}
}

void UTempoMapQueryServiceSubsystem::GetLaneData(const LaneDataRequest& Request, const TResponseDelegate<LaneDataResponse>& ResponseContinuation) const
{
	const UZoneGraphSubsystem* ZoneGraphSubsystem = GetWorld()->GetSubsystem<UZoneGraphSubsystem>();
	
	// Map from tag names (all lowercase) to tags.
	TMap<FString, FZoneGraphTag> AllTags;
	for (const auto& TagInfo : ZoneGraphSubsystem->GetTagInfos())
	{
		AllTags.Add(TagInfo.Name.ToString().ToLower(), TagInfo.Tag);
	}

	auto BuildTagMask = [&AllTags] (const google::protobuf::RepeatedPtrField<std::string>& RequestedTags)
	{
		FZoneGraphTagMask Mask;
		for (const std::string& RequestedTag : RequestedTags)
		{
			const FString RequestedName(UTF8_TO_TCHAR(RequestedTag.c_str()));
			if (const FZoneGraphTag* Tag = AllTags.Find(RequestedName.ToLower()))
			{
				Mask.Add(*Tag);
				continue;
			}
			UE_LOG(LogTempoMapQuery, Warning, TEXT("Could not find tag for name %s"), *RequestedName);
		}
		return Mask;
	};

	const FZoneGraphTagMask AnyMask = BuildTagMask(Request.tag_filter().any_tags());
	const FZoneGraphTagMask AllMask = BuildTagMask(Request.tag_filter().all_tags());
	const FZoneGraphTagMask NoneMask = BuildTagMask(Request.tag_filter().none_tags());
	
	LaneDataResponse Response;
	for (TActorIterator<AZoneGraphData> ActorIt(GetWorld()); ActorIt; ++ActorIt)
	{
		const AZoneGraphData* ZoneGraphData = *ActorIt;
		const FZoneGraphStorage& Storage = ZoneGraphData->GetStorage();
		const TArray<FZoneLaneData>& Lanes = Storage.Lanes;
		const TArray<FZoneLaneLinkData>& LaneLinks = Storage.LaneLinks;
		const TArray<FVector>& LanePoints = Storage.LanePoints;
		for (int32 LaneIdx = 0; LaneIdx < Lanes.Num(); ++LaneIdx)
		{
			const FZoneLaneData& Lane = Lanes[LaneIdx];
			
			// Ignore if if the tag filter doesn't match.
			const bool bContainsAny = Request.tag_filter().any_tags().size() > 0 ? Lane.Tags.CompareMasks(AnyMask, EZoneLaneTagMaskComparison::Any) : true;
			const bool bContainsAll = Request.tag_filter().all_tags().size() > 0 ? Lane.Tags.CompareMasks(AllMask, EZoneLaneTagMaskComparison::All) : true;
			const bool bContainsNone = Request.tag_filter().none_tags().size() > 0 ? Lane.Tags.CompareMasks(NoneMask, EZoneLaneTagMaskComparison::Not) : true;
			if (!(bContainsAny && bContainsAll && bContainsNone))
			{
				// This lane is ignored by the tag filter.
				continue;
			}

			// Ignore if all points on the lane are not within the requested radius of the requested center.
			if (Request.has_center() && Request.radius() > 0.0)
			{
				const FVector2D Center = QuantityConverter<M2CM, R2L>::Convert(FVector2D(Request.center().x(), Request.center().y()));
				const double MaxRangeSquared = QuantityConverter<M2CM>::Convert(Request.radius()) * QuantityConverter<M2CM>::Convert(Request.radius());
				bool bIgnoredByLocation = true;
				for (int32 PointIdx = Lane.PointsBegin; PointIdx < Lane.PointsEnd; ++PointIdx)
				{
					if ((FVector2D(LanePoints[PointIdx]) - Center).SizeSquared() <= MaxRangeSquared)
					{
						bIgnoredByLocation = false;
						break;
					}
				}
				if (bIgnoredByLocation)
				{
					continue;
				}
			}
			
			auto* lane = Response.add_lanes();
			lane->set_id(LaneIdx);
			for (int32 PointIdx = Lane.PointsBegin; PointIdx < Lane.PointsEnd; ++PointIdx)
			{
				const FVector& Point = LanePoints[PointIdx];
				auto* point = lane->add_center_points();
				const FVector PointRightHandedM = QuantityConverter<CM2M, L2R>::Convert(Point);
				point->set_x(PointRightHandedM.X);
				point->set_y(PointRightHandedM.Y);
				point->set_z(PointRightHandedM.Z);
			}
			lane->set_width(QuantityConverter<CM2M>::Convert(Lane.Width));
			for (int32 LaneLinkIdx = Lane.LinksBegin; LaneLinkIdx < Lane.LinksEnd; ++LaneLinkIdx)
			{
				const FZoneLaneLinkData& LaneLink = LaneLinks[LaneLinkIdx];
				auto* connection = lane->add_connections();
				connection->set_id(LaneLink.DestLaneIndex);
				connection->set_relationship(LaneRelationshipFromLinkType(LaneLink.Type));
			}
			const FZoneGraphTagMask LaneTagFilter = Lane.Tags;
			for (const auto& Tag : AllTags)
			{
				if (LaneTagFilter.Contains(Tag.Value))
				{
					lane->add_tags(std::string(TCHAR_TO_UTF8(*Tag.Key.ToLower())));
				}
			}
		}
	}
	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoMapQueryServiceSubsystem::GetLaneAccessibility(const TempoMapQuery::LaneAccessibilityRequest& Request, const TResponseDelegate<TempoMapQuery::LaneAccessibilityResponse>& ResponseContinuation) const
{
	LaneAccessibilityResponse Response;
	Response.set_accessibility(GetLaneAccessibility(Request.to_id()));
	ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}

void UTempoMapQueryServiceSubsystem::StreamLaneAccessibility(const TempoMapQuery::LaneAccessibilityRequest& Request, const TResponseDelegate<TempoMapQuery::LaneAccessibilityResponse>& ResponseContinuation)
{
	if (FLaneAccessibilityInfo* AccessibilityInfo = PendingLaneAccessibilityRequests.Find(Request.to_id()))
	{
		AccessibilityInfo->ResponseContinuations.Add(ResponseContinuation);
	}
	else
	{
		PendingLaneAccessibilityRequests.Add(Request.to_id(), FLaneAccessibilityInfo(GetLaneAccessibility(Request.to_id()), ResponseContinuation));
	}
}

TempoMapQuery::LaneAccessibility UTempoMapQueryServiceSubsystem::GetLaneAccessibility(const int32 LaneId) const
{
	const UMassTrafficSubsystem* MassTrafficSubsystem = GetWorld()->GetSubsystem<UMassTrafficSubsystem>();
	for (TActorIterator<AZoneGraphData> ActorIt(GetWorld()); ActorIt; ++ActorIt)
	{
		const AZoneGraphData* ZoneGraphData = *ActorIt;
		const FZoneGraphStorage& Storage = ZoneGraphData->GetStorage();
		const FMassTrafficZoneGraphData* MassTrafficZoneGraphData = MassTrafficSubsystem->GetTrafficZoneGraphData(Storage.DataHandle);
		if (const FZoneGraphTrafficLaneData* TrafficLaneData = MassTrafficZoneGraphData->GetTrafficLaneData(LaneId))
		{
			if (TrafficLaneData->ConstData.bIsIntersectionLane)
			{
				if (TrafficLaneData->HasTrafficLightAtLaneStart())
				{
					if (TrafficLaneData->bIsOpen)
					{
						if (!TrafficLaneData->bIsAboutToClose)
						{
							return TempoMapQuery::GREEN;
						}
						return TempoMapQuery::YELLOW;
					}
					return TempoMapQuery::RED;
				}
				if (TrafficLaneData->HasYieldSignAtLaneStart())
				{
					return TempoMapQuery::YIELD_SIGN;
				}
				if (TrafficLaneData->HasStopSignAtLaneStart())
				{
					return TempoMapQuery::STOP_SIGN;
				}
			}
			return TempoMapQuery::NO_TRAFFIC_CONTROL;
		}
	}
	return TempoMapQuery::LA_UNKNOWN;
}

void UTempoMapQueryServiceSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	for (auto AccessibilityRequestIt = PendingLaneAccessibilityRequests.CreateIterator(); AccessibilityRequestIt; ++AccessibilityRequestIt)
	{
		const int32 LaneID = AccessibilityRequestIt->Key;
		const FLaneAccessibilityInfo& AccessibilityInfo = AccessibilityRequestIt->Value;
		const TempoMapQuery::LaneAccessibility CurrentAccessibility = GetLaneAccessibility(LaneID);
		if (CurrentAccessibility != AccessibilityInfo.LastKnownAccessibility)
		{
			for (const auto& ResponseContinuation : AccessibilityInfo.ResponseContinuations)
			{
				TempoMapQuery::LaneAccessibilityResponse Response;
				Response.set_accessibility(CurrentAccessibility);
				ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
			}
			AccessibilityRequestIt.RemoveCurrent();
		}
	}
}

TStatId UTempoMapQueryServiceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTempoMapQueryServiceSubsystem, STATGROUP_Tickables);
}
