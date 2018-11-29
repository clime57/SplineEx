// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Spline.cpp
=============================================================================*/

#include "SplineExComponent.h"
#include "UObject/EditorObjectVersion.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "SceneManagement.h"

#define SPLINE_FAST_BOUNDS_CALCULATION 0

const FInterpCurvePointVector USplineExComponent::DummyPointPosition(0.0f, FVector::ZeroVector, FVector::ForwardVector, FVector::ForwardVector, CIM_Constant);
const FInterpCurvePointQuat USplineExComponent::DummyPointRotation(0.0f, FQuat::Identity);
const FInterpCurvePointVector USplineExComponent::DummyPointScale(0.0f, FVector::OneVector);


USplineExComponent::USplineExComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bAllowSplineEditingPerInstance_DEPRECATED(true)
	, ReparamStepsPerSegment(10)
	, Duration(1.0f)
	, bStationaryEndpoints(false)
	, bSplineHasBeenEdited(false)
	, bModifiedByConstructionScript(false)
	, bInputSplinePointsToConstructionScript(false)
	, bDrawDebug(true)
	, bClosedLoop(false)
	, DefaultUpVector(FVector::UpVector)
#if WITH_EDITORONLY_DATA
	, EditorUnselectedSplineSegmentColor(FLinearColor(1.0f, 1.0f, 1.0f))
	, EditorSelectedSplineSegmentColor(FLinearColor(1.0f, 0.0f, 0.0f))
	, bAllowDiscontinuousSpline(false)
	, bShouldVisualizeScale(false)
	, ScaleVisualizationWidth(30.0f)
#endif
{
	SplineCurves.Position.Points.Reset(10);
	SplineCurves.Rotation.Points.Reset(10);
	SplineCurves.Scale.Points.Reset(10);

	SplineCurves.Position.Points.Emplace(0.0f, FVector(0, 0, 0), FVector::ZeroVector, FVector::ZeroVector, CIM_CurveAuto);
	SplineCurves.Rotation.Points.Emplace(0.0f, FQuat::Identity, FQuat::Identity, FQuat::Identity, CIM_CurveAuto);
	SplineCurves.Scale.Points.Emplace(0.0f, FVector(1.0f), FVector::ZeroVector, FVector::ZeroVector, CIM_CurveAuto);

	SplineCurves.Position.Points.Emplace(1.0f, FVector(100, 0, 0), FVector::ZeroVector, FVector::ZeroVector, CIM_CurveAuto);
	SplineCurves.Rotation.Points.Emplace(1.0f, FQuat::Identity, FQuat::Identity, FQuat::Identity, CIM_CurveAuto);
	SplineCurves.Scale.Points.Emplace(1.0f, FVector(1.0f), FVector::ZeroVector, FVector::ZeroVector, CIM_CurveAuto);

	UpdateSpline();

	// Set these deprecated values up so that old assets with default values load correctly (and are subsequently upgraded during Serialize)
	SplineInfo_DEPRECATED = SplineCurves.Position;
	SplineRotInfo_DEPRECATED = SplineCurves.Rotation;
	SplineScaleInfo_DEPRECATED = SplineCurves.Scale;
	SplineReparamTable_DEPRECATED = SplineCurves.ReparamTable;
}


//EInterpCurveMode ConvertSplinePointTypeToInterpCurveMode(ESplinePointType::Type SplinePointType)
//{
//	switch (SplinePointType)
//	{
//		case ESplinePointType::Linear:				return CIM_Linear;
//		case ESplinePointType::Curve:				return CIM_CurveAuto;
//		case ESplinePointType::Constant:			return CIM_Constant;
//		case ESplinePointType::CurveCustomTangent:	return CIM_CurveUser;
//		case ESplinePointType::CurveClamped:		return CIM_CurveAutoClamped;
//
//		default:									return CIM_Unknown;
//	}
//}
//
//ESplinePointType::Type ConvertInterpCurveModeToSplinePointType(EInterpCurveMode InterpCurveMode)
//{
//	switch (InterpCurveMode)
//	{
//		case CIM_Linear:			return ESplinePointType::Linear;
//		case CIM_CurveAuto:			return ESplinePointType::Curve;
//		case CIM_Constant:			return ESplinePointType::Constant;
//		case CIM_CurveUser:			return ESplinePointType::CurveCustomTangent;
//		case CIM_CurveAutoClamped:	return ESplinePointType::CurveClamped;
//
//		default:					return ESplinePointType::Constant;
//	}
//}


void USplineExComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);

	// Move points to new properties
	if (Ar.CustomVer(FEditorObjectVersion::GUID) < FEditorObjectVersion::SplineComponentCurvesInStruct &&
		Ar.IsLoading())
	{
		SplineCurves.Position = SplineInfo_DEPRECATED;
		SplineCurves.Rotation = SplineRotInfo_DEPRECATED;
		SplineCurves.Scale = SplineScaleInfo_DEPRECATED;
		SplineCurves.ReparamTable = SplineReparamTable_DEPRECATED;
	}

	// Support old resources which don't have the rotation and scale splines present
	const int32 ArchiveUE4Version = Ar.UE4Ver();
	if (ArchiveUE4Version < VER_UE4_INTERPCURVE_SUPPORTS_LOOPING)
	{
		int32 NumPoints = SplineCurves.Position.Points.Num();

		// The start point is no longer cloned as the endpoint when the spline is looped, so remove the extra endpoint if present
		const bool bHasExtraEndpoint = bClosedLoop && (SplineCurves.Position.Points[0].OutVal == SplineCurves.Position.Points[NumPoints - 1].OutVal);

		if (bHasExtraEndpoint)
		{
			SplineCurves.Position.Points.RemoveAt(NumPoints - 1, 1, false);
			NumPoints--;
		}

		// Fill the other two splines with some defaults
		SplineCurves.Rotation.Points.Reset(NumPoints);
		SplineCurves.Scale.Points.Reset(NumPoints);
		for (int32 Count = 0; Count < NumPoints; Count++)
		{
			SplineCurves.Rotation.Points.Emplace(0.0f, FQuat::Identity, FQuat::Identity, FQuat::Identity, CIM_CurveAuto);
			SplineCurves.Scale.Points.Emplace(0.0f, FVector(1.0f), FVector::ZeroVector, FVector::ZeroVector, CIM_CurveAuto);
		}

		UpdateSpline();
	}
}


//void FSplineCurves::UpdateSpline(bool bClosedLoop, bool bStationaryEndpoints, int32 ReparamStepsPerSegment, bool bLoopPositionOverride, float LoopPosition, const FVector& Scale3D)
//{
//	const int32 NumPoints = Position.Points.Num();
//	check(Rotation.Points.Num() == NumPoints && Scale.Points.Num() == NumPoints);
//
//#if DO_CHECK
//	// Ensure input keys are strictly ascending
//	for (int32 Index = 1; Index < NumPoints; Index++)
//	{
//		ensureAlways(Position.Points[Index - 1].InVal < Position.Points[Index].InVal);
//	}
//#endif

//	// Ensure splines' looping status matches with that of the spline component
//	if (bClosedLoop)
//	{
//		const float LastKey = Position.Points.Num() > 0 ? Position.Points.Last().InVal : 0.0f;
//		const float LoopKey = bLoopPositionOverride ? LoopPosition : LastKey + 1.0f;
//		Position.SetLoopKey(LoopKey);
//		Rotation.SetLoopKey(LoopKey);
//		Scale.SetLoopKey(LoopKey);
//	}
//	else
//	{
//		Position.ClearLoopKey();
//		Rotation.ClearLoopKey();
//		Scale.ClearLoopKey();
//	}
//
//	// Automatically set the tangents on any CurveAuto keys
//	Position.AutoSetTangents(0.0f, bStationaryEndpoints);
//	Rotation.AutoSetTangents(0.0f, bStationaryEndpoints);
//	Scale.AutoSetTangents(0.0f, bStationaryEndpoints);
//
//	// Now initialize the spline reparam table
//	const int32 NumSegments = bClosedLoop ? NumPoints : NumPoints - 1;
//
//	// Start by clearing it
//	ReparamTable.Points.Reset(NumSegments * ReparamStepsPerSegment + 1);
//	float AccumulatedLength = 0.0f;
//	for (int32 SegmentIndex = 0; SegmentIndex < NumSegments; ++SegmentIndex)
//	{
//		for (int32 Step = 0; Step < ReparamStepsPerSegment; ++Step)
//		{
//			const float Param = static_cast<float>(Step) / ReparamStepsPerSegment;
//			const float SegmentLength = (Step == 0) ? 0.0f : GetSegmentLength(SegmentIndex, Param, bClosedLoop, Scale3D);
//
//			ReparamTable.Points.Emplace(SegmentLength + AccumulatedLength, SegmentIndex + Param, 0.0f, 0.0f, CIM_Linear);
//		}
//		AccumulatedLength += GetSegmentLength(SegmentIndex, 1.0f, bClosedLoop, Scale3D);
//	}
//
//	ReparamTable.Points.Emplace(AccumulatedLength, static_cast<float>(NumSegments), 0.0f, 0.0f, CIM_Linear);
//}

void USplineExComponent::UpdateSpline()
{
	SplineCurves.UpdateSpline(bClosedLoop, bStationaryEndpoints, ReparamStepsPerSegment, bLoopPositionOverride, LoopPosition, GetComponentTransform().GetScale3D());

#if !UE_BUILD_SHIPPING
	if (bDrawDebug)
	{
		MarkRenderStateDirty();
	}
#endif
}

//float FSplineCurves::GetSegmentLength(const int32 Index, const float Param, bool bClosedLoop, const FVector& Scale3D) const
//{
//	const int32 NumPoints = Position.Points.Num();
//	const int32 LastPoint = NumPoints - 1;
//
//	check(Index >= 0 && ((bClosedLoop && Index < NumPoints) || (!bClosedLoop && Index < LastPoint)));
//	check(Param >= 0.0f && Param <= 1.0f);
//
//	// Evaluate the length of a Hermite spline segment.
//	// This calculates the integral of |dP/dt| dt, where P(t) is the spline equation with components (x(t), y(t), z(t)).
//	// This isn't solvable analytically, so we use a numerical method (Legendre-Gauss quadrature) which performs very well
//	// with functions of this type, even with very few samples.  In this case, just 5 samples is sufficient to yield a
//	// reasonable result.
//
//	struct FLegendreGaussCoefficient
//	{
//		float Abscissa;
//		float Weight;
//	};
//
//	static const FLegendreGaussCoefficient LegendreGaussCoefficients[] =
//	{
//		{ 0.0f, 0.5688889f },
//		{ -0.5384693f, 0.47862867f },
//		{ 0.5384693f, 0.47862867f },
//		{ -0.90617985f, 0.23692688f },
//		{ 0.90617985f, 0.23692688f }
//	};
//
//	const auto& StartPoint = Position.Points[Index];
//	const auto& EndPoint = Position.Points[Index == LastPoint ? 0 : Index + 1];
//
//	const auto& P0 = StartPoint.OutVal;
//	const auto& T0 = StartPoint.LeaveTangent;
//	const auto& P1 = EndPoint.OutVal;
//	const auto& T1 = EndPoint.ArriveTangent;
//
//	// Special cases for linear or constant segments
//	if (StartPoint.InterpMode == CIM_Linear)
//	{
//		return ((P1 - P0) * Scale3D).Size() * Param;
//	}
//	else if (StartPoint.InterpMode == CIM_Constant)
//	{
//		return 0.0f;
//	}
//
//	// Cache the coefficients to be fed into the function to calculate the spline derivative at each sample point as they are constant.
//	const FVector Coeff1 = ((P0 - P1) * 2.0f + T0 + T1) * 3.0f;
//	const FVector Coeff2 = (P1 - P0) * 6.0f - T0 * 4.0f - T1 * 2.0f;
//	const FVector Coeff3 = T0;
//
//	const float HalfParam = Param * 0.5f;
//
//	float Length = 0.0f;
//	for (const auto& LegendreGaussCoefficient : LegendreGaussCoefficients)
//	{
//		// Calculate derivative at each Legendre-Gauss sample, and perform a weighted sum
//		const float Alpha = HalfParam * (1.0f + LegendreGaussCoefficient.Abscissa);
//		const FVector Derivative = ((Coeff1 * Alpha + Coeff2) * Alpha + Coeff3) * Scale3D;
//		Length += Derivative.Size() * LegendreGaussCoefficient.Weight;
//	}
//	Length *= HalfParam;
//
//	return Length;
//}

float USplineExComponent::GetSegmentLength(const int32 Index, const float Param) const
{
	return SplineCurves.GetSegmentLength(Index, Param, bClosedLoop, GetComponentTransform().GetScale3D());
}

float USplineExComponent::GetSegmentParamFromLength(const int32 Index, const float Length, const float SegmentLength) const
{
	if (SegmentLength == 0.0f)
	{
		return 0.0f;
	}

	// Given a function P(x) which yields points along a spline with x = 0...1, we can define a function L(t) to be the
	// Euclidian length of the spline from P(0) to P(t):
	//
	//    L(t) = integral of |dP/dt| dt
	//         = integral of sqrt((dx/dt)^2 + (dy/dt)^2 + (dz/dt)^2) dt
	//
	// This method evaluates the inverse of this function, i.e. given a length d, it obtains a suitable value for t such that:
	//    L(t) - d = 0
	//
	// We use Newton-Raphson to iteratively converge on the result:
	//
	//    t' = t - f(t) / (df/dt)
	//
	// where: t is an initial estimate of the result, obtained through basic linear interpolation,
	//        f(t) is the function whose root we wish to find = L(t) - d,
	//        (df/dt) = d(L(t))/dt = |dP/dt|

	// TODO: check if this works OK with delta InVal != 1.0f

	const int32 NumPoints = SplineCurves.Position.Points.Num();
	const int32 LastPoint = NumPoints - 1;

	check(Index >= 0 && ((bClosedLoop && Index < NumPoints) || (!bClosedLoop && Index < LastPoint)));
	check(Length >= 0.0f && Length <= SegmentLength);

	float Param = Length / SegmentLength;  // initial estimate for t

	// two iterations of Newton-Raphson is enough
	for (int32 Iteration = 0; Iteration < 2; ++Iteration)
	{
		float TangentMagnitude = SplineCurves.Position.EvalDerivative(Index + Param, FVector::ZeroVector).Size();
		if (TangentMagnitude > 0.0f)
		{
			Param -= (GetSegmentLength(Index, Param) - Length) / TangentMagnitude;
			Param = FMath::Clamp(Param, 0.0f, 1.0f);
		}
	}

	return Param;
}


FVector USplineExComponent::GetLocationAtSplineInputKey(float InKey, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	FVector Location = SplineCurves.Position.Eval(InKey, FVector::ZeroVector);

	if (CoordinateSpace == ESplineCoordinateSpace::World)
	{
		Location = GetComponentTransform().TransformPosition(Location);
	}

	return Location;
}


FVector USplineExComponent::GetTangentAtSplineInputKey(float InKey, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	FVector Tangent = SplineCurves.Position.EvalDerivative(InKey, FVector::ZeroVector);

	if (CoordinateSpace == ESplineCoordinateSpace::World)
	{
		Tangent = GetComponentTransform().TransformVector(Tangent);
	}

	return Tangent;
}


FVector USplineExComponent::GetDirectionAtSplineInputKey(float InKey, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	FVector Direction = SplineCurves.Position.EvalDerivative(InKey, FVector::ZeroVector).GetSafeNormal();

	if (CoordinateSpace == ESplineCoordinateSpace::World)
	{
		Direction = GetComponentTransform().TransformVectorNoScale(Direction);
	}

	return Direction;
}


FRotator USplineExComponent::GetRotationAtSplineInputKey(float InKey, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	return GetQuaternionAtSplineInputKey(InKey, CoordinateSpace).Rotator();
}


FQuat USplineExComponent::GetQuaternionAtSplineInputKey(float InKey, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	FQuat Quat = SplineCurves.Rotation.Eval(InKey, FQuat::Identity);
	Quat.Normalize();

	const FVector Direction = SplineCurves.Position.EvalDerivative(InKey, FVector::ZeroVector).GetSafeNormal();
	const FVector UpVector = Quat.RotateVector(DefaultUpVector);//@Clime

	FQuat Rot = (FRotationMatrix::MakeFromXZ(Direction, UpVector)).ToQuat();

	if (CoordinateSpace == ESplineCoordinateSpace::World)
	{
		Rot = GetComponentTransform().GetRotation() * Rot;
	}

	return Rot;
}


FVector USplineExComponent::GetUpVectorAtSplineInputKey(float InKey, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const FQuat Quat = GetQuaternionAtSplineInputKey(InKey, ESplineCoordinateSpace::Local);
	FVector UpVector = Quat.RotateVector(FVector::UpVector);

	if (CoordinateSpace == ESplineCoordinateSpace::World)
	{
		UpVector = GetComponentTransform().TransformVectorNoScale(UpVector);
	}

	return UpVector;
}


FVector USplineExComponent::GetRightVectorAtSplineInputKey(float InKey, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const FQuat Quat = GetQuaternionAtSplineInputKey(InKey, ESplineCoordinateSpace::Local);
	FVector RightVector = Quat.RotateVector(FVector::RightVector);

	if (CoordinateSpace == ESplineCoordinateSpace::World)
	{
		RightVector = GetComponentTransform().TransformVectorNoScale(RightVector);
	}

	return RightVector;
}


FTransform USplineExComponent::GetTransformAtSplineInputKey(float InKey, ESplineCoordinateSpace::Type CoordinateSpace, bool bUseScale) const
{
	const FVector Location(GetLocationAtSplineInputKey(InKey, ESplineCoordinateSpace::Local));
	const FQuat Rotation(GetQuaternionAtSplineInputKey(InKey, ESplineCoordinateSpace::Local));
	const FVector Scale = bUseScale ? GetScaleAtSplineInputKey(InKey) : FVector(1.0f);

	FTransform Transform(Rotation, Location, Scale);

	if (CoordinateSpace == ESplineCoordinateSpace::World)
	{
		Transform = Transform * GetComponentTransform();
	}

	return Transform;
}


float USplineExComponent::GetRollAtSplineInputKey(float InKey, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	return GetRotationAtSplineInputKey(InKey, CoordinateSpace).Roll;
}


FVector USplineExComponent::GetScaleAtSplineInputKey(float InKey) const
{
	const FVector Scale = SplineCurves.Scale.Eval(InKey, FVector(1.0f));
	return Scale;
}


void USplineExComponent::SetClosedLoop(bool bInClosedLoop, bool bUpdateSpline)
{
	bClosedLoop = bInClosedLoop;
	bLoopPositionOverride = false;
	if (bUpdateSpline)
	{
		UpdateSpline();
	}
}


void USplineExComponent::SetClosedLoopAtPosition(bool bInClosedLoop, float Key, bool bUpdateSpline)
{
	bClosedLoop = bInClosedLoop;
	bLoopPositionOverride = bInClosedLoop;
	LoopPosition = Key;

	if (bUpdateSpline)
	{
		UpdateSpline();
	}
}


bool USplineExComponent::IsClosedLoop() const
{
	return bClosedLoop;
}


void USplineExComponent::SetUnselectedSplineSegmentColor(const FLinearColor& Color)
{
#if WITH_EDITORONLY_DATA
	EditorUnselectedSplineSegmentColor = Color;
#endif
}


void USplineExComponent::SetSelectedSplineSegmentColor(const FLinearColor& Color)
{
#if WITH_EDITORONLY_DATA
	EditorSelectedSplineSegmentColor = Color;
#endif
}


void USplineExComponent::SetDrawDebug(bool bShow)
{
	bDrawDebug = bShow;
	MarkRenderStateDirty();
}


void USplineExComponent::ClearSplinePoints(bool bUpdateSpline)
{
	SplineCurves.Position.Points.Reset();
	SplineCurves.Rotation.Points.Reset();
	SplineCurves.Scale.Points.Reset();
	if (bUpdateSpline)
	{
		UpdateSpline();
	}
}


static int32 UpperBound(const TArray<FInterpCurvePoint<FVector>>& SplinePoints, float Value)
{
	int32 Count = SplinePoints.Num();
	int32 First = 0;

	while (Count > 0)
	{
		const int32 Middle = Count / 2;
		if (Value >= SplinePoints[First + Middle].InVal)
		{
			First += Middle + 1;
			Count -= Middle + 1;
		}
		else
		{
			Count = Middle;
		}
	}

	return First;
}


void USplineExComponent::AddPoint(const FSplinePoint& InSplinePoint, bool bUpdateSpline)
{
	const int32 Index = UpperBound(SplineCurves.Position.Points, InSplinePoint.InputKey);

	SplineCurves.Position.Points.Insert(FInterpCurvePoint<FVector>(
		InSplinePoint.InputKey,
		InSplinePoint.Position,
		InSplinePoint.ArriveTangent,
		InSplinePoint.LeaveTangent,
		ConvertSplinePointTypeToInterpCurveMode(InSplinePoint.Type)
		),
		Index);

	SplineCurves.Rotation.Points.Insert(FInterpCurvePoint<FQuat>(
		InSplinePoint.InputKey,
		InSplinePoint.Rotation.Quaternion(),
		FQuat::Identity,
		FQuat::Identity,
		CIM_CurveAuto
		),
		Index);

	SplineCurves.Scale.Points.Insert(FInterpCurvePoint<FVector>(
		InSplinePoint.InputKey,
		InSplinePoint.Scale,
		FVector::ZeroVector,
		FVector::ZeroVector,
		CIM_CurveAuto
		),
		Index);

	if (bLoopPositionOverride && LoopPosition <= SplineCurves.Position.Points.Last().InVal)
	{
		bLoopPositionOverride = false;
	}

	if (bUpdateSpline)
	{
		UpdateSpline();
	}
}


void USplineExComponent::AddPoints(const TArray<FSplinePoint>& InSplinePoints, bool bUpdateSpline)
{
	const int32 NumPoints = SplineCurves.Position.Points.Num();
	SplineCurves.Position.Points.Reserve(NumPoints + InSplinePoints.Num());

	for (const auto& SplinePoint : InSplinePoints)
	{
		AddPoint(SplinePoint, false);
	}

	if (bUpdateSpline)
	{
		UpdateSpline();
	}
}


void USplineExComponent::AddSplinePoint(const FVector& Position, ESplineCoordinateSpace::Type CoordinateSpace, bool bUpdateSpline)
{
	const FVector TransformedPosition = (CoordinateSpace == ESplineCoordinateSpace::World) ?
		GetComponentTransform().InverseTransformPosition(Position) : Position;

	// Add the spline point at the end of the array, adding 1.0 to the current last input key.
	// This continues the former behavior in which spline points had to be separated by an interval of 1.0.
	const float InKey = SplineCurves.Position.Points.Num() ? SplineCurves.Position.Points.Last().InVal + 1.0f : 0.0f;

	SplineCurves.Position.Points.Emplace(InKey, TransformedPosition, FVector::ZeroVector, FVector::ZeroVector, CIM_CurveAuto);
	SplineCurves.Rotation.Points.Emplace(InKey, FQuat::Identity, FQuat::Identity, FQuat::Identity, CIM_CurveAuto);
	SplineCurves.Scale.Points.Emplace(InKey, FVector(1.0f), FVector::ZeroVector, FVector::ZeroVector, CIM_CurveAuto);

	if (bLoopPositionOverride)
	{
		LoopPosition += 1.0f;
	}

	if (bUpdateSpline)
	{
		UpdateSpline();
	}
}

void USplineExComponent::AddSplinePointAtIndex(const FVector& Position, int32 Index, ESplineCoordinateSpace::Type CoordinateSpace, bool bUpdateSpline)
{
	const FVector TransformedPosition = (CoordinateSpace == ESplineCoordinateSpace::World) ?
		GetComponentTransform().InverseTransformPosition(Position) : Position;

	int32 NumPoints = SplineCurves.Position.Points.Num();

	if (Index >= 0 && Index <= NumPoints)
	{
		const float InKey = (Index == 0) ? 0.0f : SplineCurves.Position.Points[Index - 1].InVal + 1.0f;

		SplineCurves.Position.Points.Insert(FInterpCurvePoint<FVector>(InKey, TransformedPosition, FVector::ZeroVector, FVector::ZeroVector, CIM_CurveAuto), Index);
		SplineCurves.Rotation.Points.Insert(FInterpCurvePoint<FQuat>(InKey, FQuat::Identity, FQuat::Identity, FQuat::Identity, CIM_CurveAuto), Index);
		SplineCurves.Scale.Points.Insert(FInterpCurvePoint<FVector>(InKey, FVector(1.0f), FVector::ZeroVector, FVector::ZeroVector, CIM_CurveAuto), Index);
		NumPoints++;

		// Adjust subsequent points' input keys to make room for the value just added
		for (int I = Index + 1; I < NumPoints; ++I)
		{
			SplineCurves.Position.Points[I].InVal += 1.0f;
			SplineCurves.Rotation.Points[I].InVal += 1.0f;
			SplineCurves.Scale.Points[I].InVal += 1.0f;
		}

		if (bLoopPositionOverride)
		{
			LoopPosition += 1.0f;
		}
	}

	if (bUpdateSpline)
	{
		UpdateSpline();
	}
}

void USplineExComponent::RemoveSplinePoint(int32 Index, bool bUpdateSpline)
{
	int32 NumPoints = SplineCurves.Position.Points.Num();

	if (Index >= 0 && Index < NumPoints)
	{
		SplineCurves.Position.Points.RemoveAt(Index, 1, false);
		SplineCurves.Rotation.Points.RemoveAt(Index, 1, false);
		SplineCurves.Scale.Points.RemoveAt(Index, 1, false);
		NumPoints--;

		// Adjust all following spline point input keys to close the gap left by the removed point
		while (Index < NumPoints)
		{
			SplineCurves.Position.Points[Index].InVal -= 1.0f;
			SplineCurves.Rotation.Points[Index].InVal -= 1.0f;
			SplineCurves.Scale.Points[Index].InVal -= 1.0f;
			Index++;
		}

		if (bLoopPositionOverride)
		{
			LoopPosition -= 1.0f;
		}
	}

	if (bUpdateSpline)
	{
		UpdateSpline();
	}
}

void USplineExComponent::SetSplinePoints(const TArray<FVector>& Points, ESplineCoordinateSpace::Type CoordinateSpace, bool bUpdateSpline)
{
	const int32 NumPoints = Points.Num();
	SplineCurves.Position.Points.Reset(NumPoints);
	SplineCurves.Rotation.Points.Reset(NumPoints);
	SplineCurves.Scale.Points.Reset(NumPoints);

	float InputKey = 0.0f;
	for (const auto& Point : Points)
	{
		const FVector TransformedPoint = (CoordinateSpace == ESplineCoordinateSpace::World) ?
			GetComponentTransform().InverseTransformPosition(Point) : Point;

		SplineCurves.Position.Points.Emplace(InputKey, TransformedPoint, FVector::ZeroVector, FVector::ZeroVector, CIM_CurveAuto);
		SplineCurves.Rotation.Points.Emplace(InputKey, FQuat::Identity, FQuat::Identity, FQuat::Identity, CIM_CurveAuto);
		SplineCurves.Scale.Points.Emplace(InputKey, FVector(1.0f), FVector::ZeroVector, FVector::ZeroVector, CIM_CurveAuto);

		InputKey += 1.0f;
	}

	bLoopPositionOverride = false;

	if (bUpdateSpline)
	{
		UpdateSpline();
	}
}


void USplineExComponent::SetLocationAtSplinePoint(int32 PointIndex, const FVector& InLocation, ESplineCoordinateSpace::Type CoordinateSpace, bool bUpdateSpline)
{
	const int32 NumPoints = SplineCurves.Position.Points.Num();

	if ((PointIndex >= 0) && (PointIndex < NumPoints))
	{
		const FVector TransformedLocation = (CoordinateSpace == ESplineCoordinateSpace::World) ?
			GetComponentTransform().InverseTransformPosition(InLocation) : InLocation;

		SplineCurves.Position.Points[PointIndex].OutVal = TransformedLocation;

		if (bUpdateSpline)
		{
			UpdateSpline();
		}
	}
}


void USplineExComponent::SetTangentAtSplinePoint(int32 PointIndex, const FVector& InTangent, ESplineCoordinateSpace::Type CoordinateSpace, bool bUpdateSpline)
{
	const int32 NumPoints = SplineCurves.Position.Points.Num();

	if ((PointIndex >= 0) && (PointIndex < NumPoints))
	{
		const FVector TransformedTangent = (CoordinateSpace == ESplineCoordinateSpace::World) ?
			GetComponentTransform().InverseTransformVector(InTangent) : InTangent;

		SplineCurves.Position.Points[PointIndex].LeaveTangent = TransformedTangent;
		SplineCurves.Position.Points[PointIndex].ArriveTangent = TransformedTangent;
		SplineCurves.Position.Points[PointIndex].InterpMode = CIM_CurveUser;

		if (bUpdateSpline)
		{
			UpdateSpline();
		}
	}
}


void USplineExComponent::SetTangentsAtSplinePoint(int32 PointIndex, const FVector& InArriveTangent, const FVector& InLeaveTangent, ESplineCoordinateSpace::Type CoordinateSpace, bool bUpdateSpline)
{
	const int32 NumPoints = SplineCurves.Position.Points.Num();

	if ((PointIndex >= 0) && (PointIndex < NumPoints))
	{
		const FVector TransformedArriveTangent = (CoordinateSpace == ESplineCoordinateSpace::World) ?
			GetComponentTransform().InverseTransformVector(InArriveTangent) : InArriveTangent;
		const FVector TransformedLeaveTangent = (CoordinateSpace == ESplineCoordinateSpace::World) ?
			GetComponentTransform().InverseTransformVector(InLeaveTangent) : InLeaveTangent;

		SplineCurves.Position.Points[PointIndex].ArriveTangent = TransformedArriveTangent;
		SplineCurves.Position.Points[PointIndex].LeaveTangent = TransformedLeaveTangent;
		SplineCurves.Position.Points[PointIndex].InterpMode = CIM_CurveUser;

		if (bUpdateSpline)
		{
			UpdateSpline();
		}
	}
}


void USplineExComponent::SetUpVectorAtSplinePoint(int32 PointIndex, const FVector& InUpVector, ESplineCoordinateSpace::Type CoordinateSpace, bool bUpdateSpline)
{
	const int32 NumPoints = SplineCurves.Position.Points.Num();

	if ((PointIndex >= 0) && (PointIndex < NumPoints))
	{
		const FVector TransformedUpVector = (CoordinateSpace == ESplineCoordinateSpace::World) ?
			GetComponentTransform().InverseTransformVector(InUpVector.GetSafeNormal()) : InUpVector.GetSafeNormal();

		FQuat Quat = FQuat::FindBetween(DefaultUpVector, TransformedUpVector);//@Clime
		SplineCurves.Rotation.Points[PointIndex].OutVal = Quat;

		if (bUpdateSpline)
		{
			UpdateSpline();
		}
	}
}


ESplinePointType::Type USplineExComponent::GetSplinePointType(int32 PointIndex) const
{
	if ((PointIndex >= 0) && (PointIndex < SplineCurves.Position.Points.Num()))
	{
		return ConvertInterpCurveModeToSplinePointType(SplineCurves.Position.Points[PointIndex].InterpMode);
	}

	return ESplinePointType::Constant;
}


void USplineExComponent::SetSplinePointType(int32 PointIndex, ESplinePointType::Type Type, bool bUpdateSpline)
{
	if ((PointIndex >= 0) && (PointIndex < SplineCurves.Position.Points.Num()))
	{
		SplineCurves.Position.Points[PointIndex].InterpMode = ConvertSplinePointTypeToInterpCurveMode(Type);
		if (bUpdateSpline)
		{
			UpdateSpline();
		}
	}
}


int32 USplineExComponent::GetNumberOfSplinePoints() const
{
	// No longer returns an imaginary extra endpoint if closed loop is set
	const int32 NumPoints = SplineCurves.Position.Points.Num();
	return NumPoints;
}


FVector USplineExComponent::GetLocationAtSplinePoint(int32 PointIndex, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const FInterpCurvePointVector& Point = GetPositionPointSafe(PointIndex);
	const FVector& Location = Point.OutVal;
	return (CoordinateSpace == ESplineCoordinateSpace::World) ? GetComponentTransform().TransformPosition(Location) : Location;
}


FVector USplineExComponent::GetDirectionAtSplinePoint(int32 PointIndex, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const FInterpCurvePointVector& Point = GetPositionPointSafe(PointIndex);
	const FVector Direction = Point.LeaveTangent.GetSafeNormal();
	return (CoordinateSpace == ESplineCoordinateSpace::World) ? GetComponentTransform().TransformVector(Direction) : Direction;
}


FVector USplineExComponent::GetTangentAtSplinePoint(int32 PointIndex, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const FInterpCurvePointVector& Point = GetPositionPointSafe(PointIndex);
	const FVector& Direction = Point.LeaveTangent;
	return (CoordinateSpace == ESplineCoordinateSpace::World) ? GetComponentTransform().TransformVector(Direction) : Direction;
}


FVector USplineExComponent::GetArriveTangentAtSplinePoint(int32 PointIndex, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const FInterpCurvePointVector& Point = GetPositionPointSafe(PointIndex);
	const FVector& Direction = Point.ArriveTangent;
	return (CoordinateSpace == ESplineCoordinateSpace::World) ? GetComponentTransform().TransformVector(Direction) : Direction;
}


FVector USplineExComponent::GetLeaveTangentAtSplinePoint(int32 PointIndex, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const FInterpCurvePointVector& Point = GetPositionPointSafe(PointIndex);
	const FVector& Direction = Point.LeaveTangent;
	return (CoordinateSpace == ESplineCoordinateSpace::World) ? GetComponentTransform().TransformVector(Direction) : Direction;
}


FQuat USplineExComponent::GetQuaternionAtSplinePoint(int32 PointIndex, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const FInterpCurvePointQuat& Point = GetRotationPointSafe(PointIndex);
	return GetQuaternionAtSplineInputKey(Point.InVal, CoordinateSpace);
}


FRotator USplineExComponent::GetRotationAtSplinePoint(int32 PointIndex, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const FInterpCurvePointQuat& Point = GetRotationPointSafe(PointIndex);
	return GetRotationAtSplineInputKey(Point.InVal, CoordinateSpace);
}


FVector USplineExComponent::GetUpVectorAtSplinePoint(int32 PointIndex, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const FInterpCurvePointQuat& Point = GetRotationPointSafe(PointIndex);
	return GetUpVectorAtSplineInputKey(Point.InVal, CoordinateSpace);
}


FVector USplineExComponent::GetRightVectorAtSplinePoint(int32 PointIndex, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const FInterpCurvePointQuat& Point = GetRotationPointSafe(PointIndex);
	return GetRightVectorAtSplineInputKey(Point.InVal, CoordinateSpace);
}


float USplineExComponent::GetRollAtSplinePoint(int32 PointIndex, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const FInterpCurvePointQuat& Point = GetRotationPointSafe(PointIndex);
	return GetRollAtSplineInputKey(Point.InVal, CoordinateSpace);
}


FVector USplineExComponent::GetScaleAtSplinePoint(int32 PointIndex) const
{
	const FInterpCurvePointVector& Point = GetScalePointSafe(PointIndex);
	return Point.OutVal;
}


FTransform USplineExComponent::GetTransformAtSplinePoint(int32 PointIndex, ESplineCoordinateSpace::Type CoordinateSpace, bool bUseScale) const
{
	const FInterpCurvePointQuat& Point = GetRotationPointSafe(PointIndex);
	return GetTransformAtSplineInputKey(Point.InVal, CoordinateSpace, bUseScale);
}


void USplineExComponent::GetLocationAndTangentAtSplinePoint(int32 PointIndex, FVector& Location, FVector& Tangent, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const FInterpCurvePointQuat& Point = GetRotationPointSafe(PointIndex);
	const float InputKey = Point.InVal;
	Location = GetLocationAtSplineInputKey(InputKey, CoordinateSpace);
	Tangent = GetTangentAtSplineInputKey(InputKey, CoordinateSpace);
}


float USplineExComponent::GetDistanceAlongSplineAtSplinePoint(int32 PointIndex) const
{
	const int32 NumPoints = SplineCurves.Position.Points.Num();
	const int32 NumSegments = bClosedLoop ? NumPoints : NumPoints - 1;

	if ((PointIndex >= 0) && (PointIndex < NumSegments + 1))
	{
		return SplineCurves.ReparamTable.Points[PointIndex * ReparamStepsPerSegment].InVal;
	}

	return 0.0f;
}

//float FSplineCurves::GetSplineLength() const
//{
//	const int32 NumPoints = ReparamTable.Points.Num();
//
//	// This is given by the input of the last entry in the remap table
//	if (NumPoints > 0)
//	{
//		return ReparamTable.Points.Last().InVal;
//	}
//
//	return 0.0f;
//}

float USplineExComponent::GetSplineLength() const
{
	return SplineCurves.GetSplineLength();
}


void USplineExComponent::SetDefaultUpVector(const FVector& UpVector, ESplineCoordinateSpace::Type CoordinateSpace)
{
	if (CoordinateSpace == ESplineCoordinateSpace::World)
	{
		DefaultUpVector = GetComponentTransform().InverseTransformVector(UpVector);
	}
	else
	{
		DefaultUpVector = UpVector;
	}

	UpdateSpline();
}


FVector USplineExComponent::GetDefaultUpVector(ESplineCoordinateSpace::Type CoordinateSpace) const
{
	if (CoordinateSpace == ESplineCoordinateSpace::World)
	{
		return GetComponentTransform().TransformVector(DefaultUpVector);
	}
	else
	{
		return DefaultUpVector;
	}
}


float USplineExComponent::GetInputKeyAtDistanceAlongSpline(float Distance) const
{
	const int32 NumPoints = SplineCurves.Position.Points.Num();

	if (NumPoints < 2)
	{
		return 0.0f;
	}

	const float TimeMultiplier = Duration / (bClosedLoop ? NumPoints : (NumPoints - 1.0f));
	return SplineCurves.ReparamTable.Eval(Distance, 0.0f) * TimeMultiplier;
}


FVector USplineExComponent::GetLocationAtDistanceAlongSpline(float Distance, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = SplineCurves.ReparamTable.Eval(Distance, 0.0f);
	return GetLocationAtSplineInputKey(Param, CoordinateSpace);
}


FVector USplineExComponent::GetTangentAtDistanceAlongSpline(float Distance, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = SplineCurves.ReparamTable.Eval(Distance, 0.0f);
	return GetTangentAtSplineInputKey(Param, CoordinateSpace);
}


FVector USplineExComponent::GetDirectionAtDistanceAlongSpline(float Distance, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = SplineCurves.ReparamTable.Eval(Distance, 0.0f);
	return GetDirectionAtSplineInputKey(Param, CoordinateSpace);
}


FQuat USplineExComponent::GetQuaternionAtDistanceAlongSpline(float Distance, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = SplineCurves.ReparamTable.Eval(Distance, 0.0f);
	return GetQuaternionAtSplineInputKey(Param, CoordinateSpace);
}


FRotator USplineExComponent::GetRotationAtDistanceAlongSpline(float Distance, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = SplineCurves.ReparamTable.Eval(Distance, 0.0f);
	return GetRotationAtSplineInputKey(Param, CoordinateSpace);
}


FVector USplineExComponent::GetUpVectorAtDistanceAlongSpline(float Distance, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = SplineCurves.ReparamTable.Eval(Distance, 0.0f);
	return GetUpVectorAtSplineInputKey(Param, CoordinateSpace);
}


FVector USplineExComponent::GetRightVectorAtDistanceAlongSpline(float Distance, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = SplineCurves.ReparamTable.Eval(Distance, 0.0f);
	return GetRightVectorAtSplineInputKey(Param, CoordinateSpace);
}


float USplineExComponent::GetRollAtDistanceAlongSpline(float Distance, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = SplineCurves.ReparamTable.Eval(Distance, 0.0f);
	return GetRollAtSplineInputKey(Param, CoordinateSpace);
}


FVector USplineExComponent::GetScaleAtDistanceAlongSpline(float Distance) const
{
	const float Param = SplineCurves.ReparamTable.Eval(Distance, 0.0f);
	return GetScaleAtSplineInputKey(Param);
}


FTransform USplineExComponent::GetTransformAtDistanceAlongSpline(float Distance, ESplineCoordinateSpace::Type CoordinateSpace, bool bUseScale) const
{
	const float Param = SplineCurves.ReparamTable.Eval(Distance, 0.0f);
	return GetTransformAtSplineInputKey(Param, CoordinateSpace, bUseScale);
}


FVector USplineExComponent::GetLocationAtTime(float Time, ESplineCoordinateSpace::Type CoordinateSpace, bool bUseConstantVelocity) const
{
	if (Duration == 0.0f)
	{
		return FVector::ZeroVector;
	}

	if (bUseConstantVelocity)
	{
		return GetLocationAtDistanceAlongSpline(Time / Duration * GetSplineLength(), CoordinateSpace);
	}
	else
	{
		const int32 NumPoints = SplineCurves.Position.Points.Num();
		const int32 NumSegments = bClosedLoop ? NumPoints : NumPoints - 1;
		const float TimeMultiplier = NumSegments / Duration;
		return GetLocationAtSplineInputKey(Time * TimeMultiplier, CoordinateSpace);
	}
}


FVector USplineExComponent::GetDirectionAtTime(float Time, ESplineCoordinateSpace::Type CoordinateSpace, bool bUseConstantVelocity) const
{
	if (Duration == 0.0f)
	{
		return FVector::ZeroVector;
	}

	if (bUseConstantVelocity)
	{
		return GetDirectionAtDistanceAlongSpline(Time / Duration * GetSplineLength(), CoordinateSpace);
	}
	else
	{
		const int32 NumPoints = SplineCurves.Position.Points.Num();
		const int32 NumSegments = bClosedLoop ? NumPoints : NumPoints - 1;
		const float TimeMultiplier = NumSegments / Duration;
		return GetDirectionAtSplineInputKey(Time * TimeMultiplier, CoordinateSpace);
	}
}


FVector USplineExComponent::GetTangentAtTime(float Time, ESplineCoordinateSpace::Type CoordinateSpace, bool bUseConstantVelocity) const
{
	if (Duration == 0.0f)
	{
		return FVector::ZeroVector;
	}

	if (bUseConstantVelocity)
	{
		return GetTangentAtDistanceAlongSpline(Time / Duration * GetSplineLength(), CoordinateSpace);
	}
	else
	{
		const int32 NumPoints = SplineCurves.Position.Points.Num();
		const int32 NumSegments = bClosedLoop ? NumPoints : NumPoints - 1;
		const float TimeMultiplier = NumSegments / Duration;
		return GetTangentAtSplineInputKey(Time * TimeMultiplier, CoordinateSpace);
	}
}


FRotator USplineExComponent::GetRotationAtTime(float Time, ESplineCoordinateSpace::Type CoordinateSpace, bool bUseConstantVelocity) const
{
	if (Duration == 0.0f)
	{
		return FRotator::ZeroRotator;
	}

	if (bUseConstantVelocity)
	{
		return GetRotationAtDistanceAlongSpline(Time / Duration * GetSplineLength(), CoordinateSpace);
	}
	else
	{
		const int32 NumPoints = SplineCurves.Position.Points.Num();
		const int32 NumSegments = bClosedLoop ? NumPoints : NumPoints - 1;
		const float TimeMultiplier = NumSegments / Duration;
		return GetRotationAtSplineInputKey(Time * TimeMultiplier, CoordinateSpace);
	}
}


FQuat USplineExComponent::GetQuaternionAtTime(float Time, ESplineCoordinateSpace::Type CoordinateSpace, bool bUseConstantVelocity) const
{
	if (Duration == 0.0f)
	{
		return FQuat::Identity;
	}

	if (bUseConstantVelocity)
	{
		return GetQuaternionAtDistanceAlongSpline(Time / Duration * GetSplineLength(), CoordinateSpace);
	}
	else
	{
		const int32 NumPoints = SplineCurves.Position.Points.Num();
		const int32 NumSegments = bClosedLoop ? NumPoints : NumPoints - 1;
		const float TimeMultiplier = NumSegments / Duration;
		return GetQuaternionAtSplineInputKey(Time * TimeMultiplier, CoordinateSpace);
	}
}


FVector USplineExComponent::GetUpVectorAtTime(float Time, ESplineCoordinateSpace::Type CoordinateSpace, bool bUseConstantVelocity) const
{
	if (Duration == 0.0f)
	{
		return FVector::ZeroVector;
	}

	if (bUseConstantVelocity)
	{
		return GetUpVectorAtDistanceAlongSpline(Time / Duration * GetSplineLength(), CoordinateSpace);
	}
	else
	{
		const int32 NumPoints = SplineCurves.Position.Points.Num();
		const int32 NumSegments = bClosedLoop ? NumPoints : NumPoints - 1;
		const float TimeMultiplier = NumSegments / Duration;
		return GetUpVectorAtSplineInputKey(Time * TimeMultiplier, CoordinateSpace);
	}
}


FVector USplineExComponent::GetRightVectorAtTime(float Time, ESplineCoordinateSpace::Type CoordinateSpace, bool bUseConstantVelocity) const
{
	if (Duration == 0.0f)
	{
		return FVector::ZeroVector;
	}

	if (bUseConstantVelocity)
	{
		return GetRightVectorAtDistanceAlongSpline(Time / Duration * GetSplineLength(), CoordinateSpace);
	}
	else
	{
		const int32 NumPoints = SplineCurves.Position.Points.Num();
		const int32 NumSegments = bClosedLoop ? NumPoints : NumPoints - 1;
		const float TimeMultiplier = NumSegments / Duration;
		return GetRightVectorAtSplineInputKey(Time * TimeMultiplier, CoordinateSpace);
	}
}


float USplineExComponent::GetRollAtTime(float Time, ESplineCoordinateSpace::Type CoordinateSpace, bool bUseConstantVelocity) const
{
	if (Duration == 0.0f)
	{
		return 0.0f;
	}

	if (bUseConstantVelocity)
	{
		return GetRollAtDistanceAlongSpline(Time / Duration * GetSplineLength(), CoordinateSpace);
	}
	else
	{
		const int32 NumPoints = SplineCurves.Position.Points.Num();
		const int32 NumSegments = bClosedLoop ? NumPoints : NumPoints - 1;
		const float TimeMultiplier = NumSegments / Duration;
		return GetRollAtSplineInputKey(Time * TimeMultiplier, CoordinateSpace);
	}
}


FTransform USplineExComponent::GetTransformAtTime(float Time, ESplineCoordinateSpace::Type CoordinateSpace, bool bUseConstantVelocity, bool bUseScale) const
{
	if (Duration == 0.0f)
	{
		return FTransform::Identity;
	}

	if (bUseConstantVelocity)
	{
		return GetTransformAtDistanceAlongSpline(Time / Duration * GetSplineLength(), CoordinateSpace, bUseScale);
	}
	else
	{
		const int32 NumPoints = SplineCurves.Position.Points.Num();
		const int32 NumSegments = bClosedLoop ? NumPoints : NumPoints - 1;
		const float TimeMultiplier = NumSegments / Duration;
		return GetTransformAtSplineInputKey(Time * TimeMultiplier, CoordinateSpace, bUseScale);
	}
}


FVector USplineExComponent::GetScaleAtTime(float Time, bool bUseConstantVelocity) const
{
	if (Duration == 0.0f)
	{
		return FVector(1.0f);
	}

	if (bUseConstantVelocity)
	{
		return GetScaleAtDistanceAlongSpline(Time / Duration * GetSplineLength());
	}
	else
	{
		const int32 NumPoints = SplineCurves.Position.Points.Num();
		const int32 NumSegments = bClosedLoop ? NumPoints : NumPoints - 1;
		const float TimeMultiplier = NumSegments / Duration;
		return GetScaleAtSplineInputKey(Time * TimeMultiplier);
	}
}


float USplineExComponent::FindInputKeyClosestToWorldLocation(const FVector& WorldLocation) const
{
	const FVector LocalLocation = GetComponentTransform().InverseTransformPosition(WorldLocation);
	float Dummy;
	return SplineCurves.Position.InaccurateFindNearest(LocalLocation, Dummy);
}


FVector USplineExComponent::FindLocationClosestToWorldLocation(const FVector& WorldLocation, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = FindInputKeyClosestToWorldLocation(WorldLocation);
	return GetLocationAtSplineInputKey(Param, CoordinateSpace);
}


FVector USplineExComponent::FindDirectionClosestToWorldLocation(const FVector& WorldLocation, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = FindInputKeyClosestToWorldLocation(WorldLocation);
	return GetDirectionAtSplineInputKey(Param, CoordinateSpace);
}


FVector USplineExComponent::FindTangentClosestToWorldLocation(const FVector& WorldLocation, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = FindInputKeyClosestToWorldLocation(WorldLocation);
	return GetTangentAtSplineInputKey(Param, CoordinateSpace);
}


FQuat USplineExComponent::FindQuaternionClosestToWorldLocation(const FVector& WorldLocation, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = FindInputKeyClosestToWorldLocation(WorldLocation);
	return GetQuaternionAtSplineInputKey(Param, CoordinateSpace);
}


FRotator USplineExComponent::FindRotationClosestToWorldLocation(const FVector& WorldLocation, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = FindInputKeyClosestToWorldLocation(WorldLocation);
	return GetRotationAtSplineInputKey(Param, CoordinateSpace);
}


FVector USplineExComponent::FindUpVectorClosestToWorldLocation(const FVector& WorldLocation, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = FindInputKeyClosestToWorldLocation(WorldLocation);
	return GetUpVectorAtSplineInputKey(Param, CoordinateSpace);
}


FVector USplineExComponent::FindRightVectorClosestToWorldLocation(const FVector& WorldLocation, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = FindInputKeyClosestToWorldLocation(WorldLocation);
	return GetRightVectorAtSplineInputKey(Param, CoordinateSpace);
}


float USplineExComponent::FindRollClosestToWorldLocation(const FVector& WorldLocation, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = FindInputKeyClosestToWorldLocation(WorldLocation);
	return GetRollAtSplineInputKey(Param, CoordinateSpace);
}


FVector USplineExComponent::FindScaleClosestToWorldLocation(const FVector& WorldLocation) const
{
	const float Param = FindInputKeyClosestToWorldLocation(WorldLocation);
	return GetScaleAtSplineInputKey(Param);
}


FTransform USplineExComponent::FindTransformClosestToWorldLocation(const FVector& WorldLocation, ESplineCoordinateSpace::Type CoordinateSpace, bool bUseScale) const
{
	const float Param = FindInputKeyClosestToWorldLocation(WorldLocation);
	return GetTransformAtSplineInputKey(Param, CoordinateSpace, bUseScale);
}

#if !UE_BUILD_SHIPPING
FPrimitiveSceneProxy* USplineExComponent::CreateSceneProxy()
{
	if (!bDrawDebug)
	{
		return Super::CreateSceneProxy();
	}

	class FSplineSceneProxy final : public FPrimitiveSceneProxy
	{
	public:
		SIZE_T GetTypeHash() const override
		{
			static size_t UniquePointer;
			return reinterpret_cast<size_t>(&UniquePointer);
		}

		FSplineSceneProxy(const USplineExComponent* InComponent)
			: FPrimitiveSceneProxy(InComponent)
			, bDrawDebug(InComponent->bDrawDebug)
			, SplineInfo(InComponent->SplineCurves.Position)
#if WITH_EDITORONLY_DATA
			, LineColor(InComponent->EditorUnselectedSplineSegmentColor)
#else
			, LineColor(FLinearColor::White)
#endif
		{}

		virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_SplineSceneProxy_GetDynamicMeshElements);

			if (IsSelected())
			{
				return;
			}

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				if (VisibilityMap & (1 << ViewIndex))
				{
					const FSceneView* View = Views[ViewIndex];
					FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

					const FMatrix& LocalToWorld = GetLocalToWorld();

					// Taking into account the min and maximum drawing distance
					const float DistanceSqr = (View->ViewMatrices.GetViewOrigin() - LocalToWorld.GetOrigin()).SizeSquared();
					if (DistanceSqr < FMath::Square(GetMinDrawDistance()) || DistanceSqr > FMath::Square(GetMaxDrawDistance()))
					{
						continue;
					}

					USplineExComponent::Draw(PDI, View, SplineInfo, LocalToWorld, LineColor, SDPG_World);
				}
			}
		}

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			FPrimitiveViewRelevance Result;
			Result.bDrawRelevance = bDrawDebug && !IsSelected() && IsShown(View) && View->Family->EngineShowFlags.Splines;
			Result.bDynamicRelevance = true;
			Result.bShadowRelevance = IsShadowCast(View);
			Result.bEditorPrimitiveRelevance = UseEditorCompositing(View);
			return Result;
		}

		virtual uint32 GetMemoryFootprint(void) const override { return sizeof *this + GetAllocatedSize(); }
		uint32 GetAllocatedSize(void) const { return FPrimitiveSceneProxy::GetAllocatedSize(); }

	private:
		bool bDrawDebug;
		FInterpCurveVector SplineInfo;
		FLinearColor LineColor;
	};

	return new FSplineSceneProxy(this);
}

void USplineExComponent::Draw(FPrimitiveDrawInterface* PDI, const FSceneView* View, const FInterpCurveVector& SplineInfo, const FMatrix& LocalToWorld, const FLinearColor& LineColor, uint8 DepthPriorityGroup)
{
	const int32 GrabHandleSize = 6;
	FVector OldKeyPos(0);

	const int32 NumPoints = SplineInfo.Points.Num();
	const int32 NumSegments = SplineInfo.bIsLooped ? NumPoints : NumPoints - 1;
	for (int32 KeyIdx = 0; KeyIdx < NumSegments + 1; KeyIdx++)
	{
		const FVector NewKeyPos = LocalToWorld.TransformPosition(SplineInfo.Eval(static_cast<float>(KeyIdx), FVector(0)));

		// Draw the keypoint
		if (KeyIdx < NumPoints)
		{
			PDI->DrawPoint(NewKeyPos, LineColor, GrabHandleSize, DepthPriorityGroup);
		}

		// If not the first keypoint, draw a line to the previous keypoint.
		if (KeyIdx > 0)
		{
			// For constant interpolation - don't draw ticks - just draw dotted line.
			if (SplineInfo.Points[KeyIdx - 1].InterpMode == CIM_Constant)
			{
				// Calculate dash length according to size on screen
				const float StartW = View->WorldToScreen(OldKeyPos).W;
				const float EndW = View->WorldToScreen(NewKeyPos).W;

				const float WLimit = 10.0f;
				if (StartW > WLimit || EndW > WLimit)
				{
					const float Scale = 0.03f;
					DrawDashedLine(PDI, OldKeyPos, NewKeyPos, LineColor, FMath::Max(StartW, EndW) * Scale, DepthPriorityGroup);
				}
			}
			else
			{
				// Find position on first keyframe.
				FVector OldPos = OldKeyPos;

				// Then draw a line for each substep.
				const int32 NumSteps = 20;

				for (int32 StepIdx = 1; StepIdx <= NumSteps; StepIdx++)
				{
					const float Key = (KeyIdx - 1) + (StepIdx / static_cast<float>(NumSteps));
					const FVector NewPos = LocalToWorld.TransformPosition(SplineInfo.Eval(Key, FVector(0)));
					PDI->DrawLine(OldPos, NewPos, LineColor, DepthPriorityGroup);
					OldPos = NewPos;
				}
			}
		}

		OldKeyPos = NewKeyPos;
	}
}

FBoxSphereBounds USplineExComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (!bDrawDebug)
	{
		// Do as little as possible if not rendering anything
		return Super::CalcBounds(LocalToWorld);
	}

#if SPLINE_FAST_BOUNDS_CALCULATION
	FBox BoundingBox(0);
	for (const auto& InterpPoint : SplineCurves.Position.Points)
	{
		BoundingBox += InterpPoint.OutVal;
	}

	return FBoxSphereBounds(BoundingBox.TransformBy(LocalToWorld));
#else
	const int32 NumPoints = SplineCurves.Position.Points.Num();
	const int32 NumSegments = bClosedLoop ? NumPoints : NumPoints - 1;

	FVector Min(WORLD_MAX);
	FVector Max(-WORLD_MAX);
	for (int32 Index = 0; Index < NumSegments; Index++)
	{
		const bool bLoopSegment = (Index == NumPoints - 1);
		const int32 NextIndex = bLoopSegment ? 0 : Index + 1;
		const FInterpCurvePoint<FVector>& ThisInterpPoint = SplineCurves.Position.Points[Index];
		FInterpCurvePoint<FVector> NextInterpPoint = SplineCurves.Position.Points[NextIndex];
		if (bLoopSegment)
		{
			NextInterpPoint.InVal = ThisInterpPoint.InVal + SplineCurves.Position.LoopKeyOffset;
		}

		CurveVectorFindIntervalBounds(ThisInterpPoint, NextInterpPoint, Min, Max);
	}

	return FBoxSphereBounds(FBox(Min, Max).TransformBy(LocalToWorld));
#endif
}

#endif


/** Used to store spline data during RerunConstructionScripts */
class FSplineInstanceData : public FSceneComponentInstanceData
{
public:
	explicit FSplineInstanceData(const USplineExComponent* SourceComponent)
		: FSceneComponentInstanceData(SourceComponent)
	{
	}

	virtual void ApplyToComponent(UActorComponent* Component, const ECacheApplyPhase CacheApplyPhase) override
	{
		FSceneComponentInstanceData::ApplyToComponent(Component, CacheApplyPhase);
		CastChecked<USplineExComponent>(Component)->ApplyComponentInstanceData(this, (CacheApplyPhase == ECacheApplyPhase::PostUserConstructionScript));
	}

	FSplineCurves SplineCurves;
	FSplineCurves SplineCurvesPreUCS;
	bool bSplineHasBeenEdited;
};


FActorComponentInstanceData* USplineExComponent::GetComponentInstanceData() const
{
	FSplineInstanceData* SplineInstanceData = new FSplineInstanceData(this);
	if (bSplineHasBeenEdited)
	{
		SplineInstanceData->SplineCurves = SplineCurves;
	}
	SplineInstanceData->bSplineHasBeenEdited = bSplineHasBeenEdited;

	return SplineInstanceData;
}


void USplineExComponent::ApplyComponentInstanceData(FSplineInstanceData* SplineInstanceData, const bool bPostUCS)
{
	check(SplineInstanceData);

	if (bPostUCS)
	{
		if (bInputSplinePointsToConstructionScript)
		{
			// Don't reapply the saved state after the UCS has run if we are inputting the points to it.
			// This allows the UCS to work on the edited points and make its own changes.
			return;
		}
		else
		{
			bModifiedByConstructionScript = (SplineInstanceData->SplineCurvesPreUCS != SplineCurves);

			// If we are restoring the saved state, unmark the SplineCurves property as 'modified'.
			// We don't want to consider that these changes have been made through the UCS.
			TArray<UProperty*> Properties;
			Properties.Emplace(FindField<UProperty>(USplineExComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(USplineExComponent, SplineCurves)));
			RemoveUCSModifiedProperties(Properties);
		}
	}
	else
	{
		SplineInstanceData->SplineCurvesPreUCS = SplineCurves;
	}

	if (SplineInstanceData->bSplineHasBeenEdited)
	{
		SplineCurves = SplineInstanceData->SplineCurves;
		bModifiedByConstructionScript = false;
	}

	bSplineHasBeenEdited = SplineInstanceData->bSplineHasBeenEdited;

	UpdateSpline();
}


#if WITH_EDITOR
void USplineExComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property != nullptr)
	{
		static const FName ReparamStepsPerSegmentName = GET_MEMBER_NAME_CHECKED(USplineExComponent, ReparamStepsPerSegment);
		static const FName StationaryEndpointsName = GET_MEMBER_NAME_CHECKED(USplineExComponent, bStationaryEndpoints);
		static const FName DefaultUpVectorName = GET_MEMBER_NAME_CHECKED(USplineExComponent, DefaultUpVector);
		static const FName ClosedLoopName = GET_MEMBER_NAME_CHECKED(USplineExComponent, bClosedLoop);
		static const FName SplineHasBeenEditedName = GET_MEMBER_NAME_CHECKED(USplineExComponent, bSplineHasBeenEdited);

		const FName PropertyName(PropertyChangedEvent.Property->GetFName());
		if (PropertyName == ReparamStepsPerSegmentName ||
			PropertyName == StationaryEndpointsName ||
			PropertyName == DefaultUpVectorName ||
			PropertyName == ClosedLoopName)
		{
			UpdateSpline();
		}
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}
#endif

//void FSplinePositionLinearApproximation::Build(const FSplineCurves& InCurves, TArray<FSplinePositionLinearApproximation>& OutPoints, float InDensity)
//{
//	OutPoints.Reset();
//
//	const float SplineLength = InCurves.GetSplineLength();
//	int32 NumLinearPoints = FMath::Max((int32)(SplineLength * InDensity), 2);
//
//	for (int32 LinearPointIndex = 0; LinearPointIndex < NumLinearPoints; ++LinearPointIndex)
//	{
//		const float DistanceAlpha = (float)LinearPointIndex / (float)NumLinearPoints;
//		const float SplineDistance = SplineLength * DistanceAlpha;
//		const float Param = InCurves.ReparamTable.Eval(SplineDistance, 0.0f);
//		OutPoints.Emplace(InCurves.Position.Eval(Param, FVector::ZeroVector), Param);
//	}
//
//	OutPoints.Emplace(InCurves.Position.Points.Last().OutVal, InCurves.ReparamTable.Points.Last().OutVal);
//}