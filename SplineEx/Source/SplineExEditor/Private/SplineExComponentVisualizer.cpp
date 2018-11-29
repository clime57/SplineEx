// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "SplineExComponentVisualizer.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/InputChord.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorStyleSet.h"
#include "UnrealWidget.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "SplineExComponent.h"
#include "ScopedTransaction.h"
#include "ActorEditorUtils.h"

IMPLEMENT_HIT_PROXY(HSplineVisProxy, HComponentVisProxy);
IMPLEMENT_HIT_PROXY(HSplineKeyProxy, HSplineVisProxy);
IMPLEMENT_HIT_PROXY(HSplineSegmentProxy, HSplineVisProxy);
IMPLEMENT_HIT_PROXY(HSplineTangentHandleProxy, HSplineVisProxy);

#define LOCTEXT_NAMESPACE "SplineExComponentVisualizer"

#define VISUALIZE_SPLINE_UPVECTORS 0

/** Define commands for the spline component visualizer */
class FSplineExComponentVisualizerCommands : public TCommands<FSplineExComponentVisualizerCommands>
{
public:
	FSplineExComponentVisualizerCommands() : TCommands <FSplineExComponentVisualizerCommands>
	(
		"SplineComponentExVisualizer",	// Context name for fast lookup
		LOCTEXT("SplineComponentExVisualizer", "SplineEx Component Visualizer"),	// Localized context name for displaying
		NAME_None,	// Parent
		FEditorStyle::GetStyleSetName()
	)
	{
	}

	virtual void RegisterCommands() override
	{
		UI_COMMAND(DeleteKey, "Delete Spline Point", "Delete the currently selected spline point.", EUserInterfaceActionType::Button, FInputChord(EKeys::Delete));
		UI_COMMAND(DuplicateKey, "Duplicate Spline Point", "Duplicate the currently selected spline point.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(AddKey, "Add Spline Point Here", "Add a new spline point at the cursor location.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(ResetToUnclampedTangent, "Unclamped Tangent", "Reset the tangent for this spline point to its default unclamped value.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(ResetToClampedTangent, "Clamped Tangent", "Reset the tangent for this spline point to its default clamped value.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(SetKeyToCurve, "Curve", "Set spline point to Curve type", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(SetKeyToLinear, "Linear", "Set spline point to Linear type", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(SetKeyToConstant, "Constant", "Set spline point to Constant type", EUserInterfaceActionType::RadioButton, FInputChord());
		UI_COMMAND(VisualizeRollAndScale, "Visualize Roll and Scale", "Whether the visualization should show roll and scale on this spline.", EUserInterfaceActionType::ToggleButton, FInputChord());
		UI_COMMAND(DiscontinuousSpline, "Allow Discontinuous Splines", "Whether the visualization allows Arrive and Leave tangents to be set separately.", EUserInterfaceActionType::ToggleButton, FInputChord());
		UI_COMMAND(ResetToDefault, "Reset to Default", "Reset this spline to its archetype default.", EUserInterfaceActionType::Button, FInputChord());
	}

public:
	/** Delete key */
	TSharedPtr<FUICommandInfo> DeleteKey;

	/** Duplicate key */
	TSharedPtr<FUICommandInfo> DuplicateKey;

	/** Add key */
	TSharedPtr<FUICommandInfo> AddKey;

	/** Reset to unclamped tangent */
	TSharedPtr<FUICommandInfo> ResetToUnclampedTangent;

	/** Reset to clamped tangent */
	TSharedPtr<FUICommandInfo> ResetToClampedTangent;

	/** Set spline key to Curve type */
	TSharedPtr<FUICommandInfo> SetKeyToCurve;

	/** Set spline key to Linear type */
	TSharedPtr<FUICommandInfo> SetKeyToLinear;

	/** Set spline key to Constant type */
	TSharedPtr<FUICommandInfo> SetKeyToConstant;

	/** Whether the visualization should show roll and scale */
	TSharedPtr<FUICommandInfo> VisualizeRollAndScale;

	/** Whether we allow separate Arrive / Leave tangents, resulting in a discontinuous spline */
	TSharedPtr<FUICommandInfo> DiscontinuousSpline;

	/** Reset this spline to its default */
	TSharedPtr<FUICommandInfo> ResetToDefault;
};



FSplineExComponentVisualizer::FSplineExComponentVisualizer()
	: FComponentVisualizer()
	, LastKeyIndexSelected(INDEX_NONE)
	, SelectedSegmentIndex(INDEX_NONE)
	, SelectedTangentHandle(INDEX_NONE)
	, SelectedTangentHandleType(ESelectedTangentHandle::None)
	, bAllowDuplication(true)
{
	FSplineExComponentVisualizerCommands::Register();

	SplineComponentVisualizerActions = MakeShareable(new FUICommandList);

	SplineCurvesProperty = FindField<UProperty>(USplineExComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(USplineExComponent, SplineCurves));
}

void FSplineExComponentVisualizer::OnRegister()
{
	const auto& Commands = FSplineExComponentVisualizerCommands::Get();

	SplineComponentVisualizerActions->MapAction(
		Commands.DeleteKey,
		FExecuteAction::CreateSP(this, &FSplineExComponentVisualizer::OnDeleteKey),
		FCanExecuteAction::CreateSP(this, &FSplineExComponentVisualizer::CanDeleteKey));

	SplineComponentVisualizerActions->MapAction(
		Commands.DuplicateKey,
		FExecuteAction::CreateSP(this, &FSplineExComponentVisualizer::OnDuplicateKey),
		FCanExecuteAction::CreateSP(this, &FSplineExComponentVisualizer::IsKeySelectionValid));

	SplineComponentVisualizerActions->MapAction(
		Commands.AddKey,
		FExecuteAction::CreateSP(this, &FSplineExComponentVisualizer::OnAddKey),
		FCanExecuteAction::CreateSP(this, &FSplineExComponentVisualizer::CanAddKey));

	SplineComponentVisualizerActions->MapAction(
		Commands.ResetToUnclampedTangent,
		FExecuteAction::CreateSP(this, &FSplineExComponentVisualizer::OnResetToAutomaticTangent, CIM_CurveAuto),
		FCanExecuteAction::CreateSP(this, &FSplineExComponentVisualizer::CanResetToAutomaticTangent, CIM_CurveAuto));

	SplineComponentVisualizerActions->MapAction(
		Commands.ResetToClampedTangent,
		FExecuteAction::CreateSP(this, &FSplineExComponentVisualizer::OnResetToAutomaticTangent, CIM_CurveAutoClamped),
		FCanExecuteAction::CreateSP(this, &FSplineExComponentVisualizer::CanResetToAutomaticTangent, CIM_CurveAutoClamped));

	SplineComponentVisualizerActions->MapAction(
		Commands.SetKeyToCurve,
		FExecuteAction::CreateSP(this, &FSplineExComponentVisualizer::OnSetKeyType, CIM_CurveAuto),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FSplineExComponentVisualizer::IsKeyTypeSet, CIM_CurveAuto));

	SplineComponentVisualizerActions->MapAction(
		Commands.SetKeyToLinear,
		FExecuteAction::CreateSP(this, &FSplineExComponentVisualizer::OnSetKeyType, CIM_Linear),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FSplineExComponentVisualizer::IsKeyTypeSet, CIM_Linear));

	SplineComponentVisualizerActions->MapAction(
		Commands.SetKeyToConstant,
		FExecuteAction::CreateSP(this, &FSplineExComponentVisualizer::OnSetKeyType, CIM_Constant),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FSplineExComponentVisualizer::IsKeyTypeSet, CIM_Constant));

	SplineComponentVisualizerActions->MapAction(
		Commands.VisualizeRollAndScale,
		FExecuteAction::CreateSP(this, &FSplineExComponentVisualizer::OnSetVisualizeRollAndScale),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FSplineExComponentVisualizer::IsVisualizingRollAndScale));

	SplineComponentVisualizerActions->MapAction(
		Commands.DiscontinuousSpline,
		FExecuteAction::CreateSP(this, &FSplineExComponentVisualizer::OnSetDiscontinuousSpline),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FSplineExComponentVisualizer::IsDiscontinuousSpline));

	SplineComponentVisualizerActions->MapAction(
		Commands.ResetToDefault,
		FExecuteAction::CreateSP(this, &FSplineExComponentVisualizer::OnResetToDefault),
		FCanExecuteAction::CreateSP(this, &FSplineExComponentVisualizer::CanResetToDefault));
}

FSplineExComponentVisualizer::~FSplineExComponentVisualizer()
{
	FSplineExComponentVisualizerCommands::Unregister();
}

static float GetDashSize(const FSceneView* View, const FVector& Start, const FVector& End, float Scale)
{
	const float StartW = View->WorldToScreen(Start).W;
	const float EndW = View->WorldToScreen(End).W;

	const float WLimit = 10.0f;
	if (StartW > WLimit || EndW > WLimit)
	{
		return FMath::Max(StartW, EndW) * Scale;
	}

	return 0.0f;
}

void FSplineExComponentVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	if (const USplineExComponent* SplineComp = Cast<const USplineExComponent>(Component))
	{
		const FInterpCurveVector& SplineInfo = SplineComp->GetSplinePointsPosition();
		const USplineExComponent* EditedSplineComp = GetEditedSplineComponent();

		const USplineExComponent* Archetype = CastChecked<USplineExComponent>(SplineComp->GetArchetype());
		const bool bIsSplineEditable = !SplineComp->bModifiedByConstructionScript; // bSplineHasBeenEdited || SplineInfo == Archetype->SplineCurves.Position || SplineComp->bInputSplinePointsToConstructionScript;

		const FColor ReadOnlyColor = FColor(255, 0, 255, 255);
		const FColor NormalColor = bIsSplineEditable ? FColor(SplineComp->EditorUnselectedSplineSegmentColor.ToFColor(true)) : ReadOnlyColor;
		const FColor SelectedColor = bIsSplineEditable ? FColor(SplineComp->EditorSelectedSplineSegmentColor.ToFColor(true)) : ReadOnlyColor;
		const float GrabHandleSize = 12.0f;
		const float TangentHandleSize = 10.0f;

		// Draw the tangent handles before anything else so they will not overdraw the rest of the spline
		if (SplineComp == EditedSplineComp)
		{
			for (int32 SelectedKey : SelectedKeys)
			{
				if (SplineInfo.Points[SelectedKey].IsCurveKey())
				{
					const FVector Location = SplineComp->GetLocationAtSplinePoint(SelectedKey, ESplineCoordinateSpace::World);
					const FVector LeaveTangent = SplineComp->GetLeaveTangentAtSplinePoint(SelectedKey, ESplineCoordinateSpace::World);
					const FVector ArriveTangent = SplineComp->bAllowDiscontinuousSpline ?
						SplineComp->GetArriveTangentAtSplinePoint(SelectedKey, ESplineCoordinateSpace::World) : LeaveTangent;

					PDI->SetHitProxy(NULL);

					const float DashSize1 = GetDashSize(View, Location, Location + LeaveTangent, 0.01f);
					if (DashSize1 > 0.0f)
					{
						DrawDashedLine(PDI, Location, Location + LeaveTangent, SelectedColor, DashSize1, SDPG_Foreground);
					}

					const float DashSize2 = GetDashSize(View, Location, Location - ArriveTangent, 0.01f);
					if (DashSize2 > 0.0f)
					{
						DrawDashedLine(PDI, Location, Location - ArriveTangent, SelectedColor, DashSize2, SDPG_Foreground);
					}

					if (bIsSplineEditable)
					{
						PDI->SetHitProxy(new HSplineTangentHandleProxy(Component, SelectedKey, false));
					}
					PDI->DrawPoint(Location + LeaveTangent, SelectedColor, TangentHandleSize, SDPG_Foreground);

					if (bIsSplineEditable)
					{
						PDI->SetHitProxy(new HSplineTangentHandleProxy(Component, SelectedKey, true));
					}
					PDI->DrawPoint(Location - ArriveTangent, SelectedColor, TangentHandleSize, SDPG_Foreground);

					PDI->SetHitProxy(NULL);
				}
			}
		}

		const bool bShouldVisualizeScale = SplineComp->bShouldVisualizeScale;
		const float DefaultScale = SplineComp->ScaleVisualizationWidth;

		FVector OldKeyPos(0);
		FVector OldKeyRightVector(0);
		FVector OldKeyScale(0);

		const int32 NumPoints = SplineInfo.Points.Num();
		const int32 NumSegments = SplineInfo.bIsLooped ? NumPoints : NumPoints - 1;
		for (int32 KeyIdx = 0; KeyIdx < NumSegments + 1; KeyIdx++)
		{
			const FVector NewKeyPos = SplineComp->GetLocationAtSplinePoint(KeyIdx, ESplineCoordinateSpace::World);
			const FVector NewKeyRightVector = SplineComp->GetRightVectorAtSplinePoint(KeyIdx, ESplineCoordinateSpace::World);
			const FVector NewKeyUpVector = SplineComp->GetUpVectorAtSplinePoint(KeyIdx, ESplineCoordinateSpace::World);
			const FVector NewKeyScale = SplineComp->GetScaleAtSplinePoint(KeyIdx) * DefaultScale;

			const FColor KeyColor = (SplineComp == EditedSplineComp && SelectedKeys.Contains(KeyIdx)) ? SelectedColor : NormalColor;

			// Draw the keypoint and up/right vectors
			if (KeyIdx < NumPoints)
			{
				if (bShouldVisualizeScale)
				{
					PDI->SetHitProxy(NULL);

					PDI->DrawLine(NewKeyPos, NewKeyPos - NewKeyRightVector * NewKeyScale.Y, KeyColor, SDPG_Foreground);
					PDI->DrawLine(NewKeyPos, NewKeyPos + NewKeyRightVector * NewKeyScale.Y, KeyColor, SDPG_Foreground);
					PDI->DrawLine(NewKeyPos, NewKeyPos + NewKeyUpVector * NewKeyScale.Z, KeyColor, SDPG_Foreground);

					const int32 ArcPoints = 20;
					FVector OldArcPos = NewKeyPos + NewKeyRightVector * NewKeyScale.Y;
					for (int32 ArcIndex = 1; ArcIndex <= ArcPoints; ArcIndex++)
					{
						float Sin;
						float Cos;
						FMath::SinCos(&Sin, &Cos, ArcIndex * PI / ArcPoints);
						const FVector NewArcPos = NewKeyPos + Cos * NewKeyRightVector * NewKeyScale.Y + Sin * NewKeyUpVector * NewKeyScale.Z;
						PDI->DrawLine(OldArcPos, NewArcPos, KeyColor, SDPG_Foreground);
						OldArcPos = NewArcPos;
					}
				}

				if (bIsSplineEditable)
				{
					PDI->SetHitProxy(new HSplineKeyProxy(Component, KeyIdx));
				}
				PDI->DrawPoint(NewKeyPos, KeyColor, GrabHandleSize, SDPG_Foreground);
				PDI->SetHitProxy(NULL);
			}

			// If not the first keypoint, draw a line to the previous keypoint.
			if (KeyIdx > 0)
			{
				const FColor LineColor = (SplineComp == EditedSplineComp && SelectedKeys.Contains(KeyIdx - 1)) ? SelectedColor : NormalColor;
				if (bIsSplineEditable)
				{
					PDI->SetHitProxy(new HSplineSegmentProxy(Component, KeyIdx - 1));
				}

				// For constant interpolation - don't draw ticks - just draw dotted line.
				if (SplineInfo.Points[KeyIdx - 1].InterpMode == CIM_Constant)
				{
					const float DashSize = GetDashSize(View, OldKeyPos, NewKeyPos, 0.03f);
					if (DashSize > 0.0f)
					{
						DrawDashedLine(PDI, OldKeyPos, NewKeyPos, LineColor, DashSize, SDPG_World);
					}
				}
				else
				{
					// Find position on first keyframe.
					FVector OldPos = OldKeyPos;
					FVector OldRightVector = OldKeyRightVector;
					FVector OldScale = OldKeyScale;

					// Then draw a line for each substep.
					const int32 NumSteps = 20;

					for (int32 StepIdx = 1; StepIdx <= NumSteps; StepIdx++)
					{
						const float Key = (KeyIdx - 1) + (StepIdx / static_cast<float>(NumSteps));
						const FVector NewPos = SplineComp->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World);
						const FVector NewRightVector = SplineComp->GetRightVectorAtSplineInputKey(Key, ESplineCoordinateSpace::World);
						const FVector NewScale = SplineComp->GetScaleAtSplineInputKey(Key) * DefaultScale;

						PDI->DrawLine(OldPos, NewPos, LineColor, SDPG_Foreground);
						if (bShouldVisualizeScale)
						{
							PDI->DrawLine(OldPos - OldRightVector * OldScale.Y, NewPos - NewRightVector * NewScale.Y, LineColor, SDPG_Foreground);
							PDI->DrawLine(OldPos + OldRightVector * OldScale.Y, NewPos + NewRightVector * NewScale.Y, LineColor, SDPG_Foreground);

							#if VISUALIZE_SPLINE_UPVECTORS
							const FVector NewUpVector = SplineComp->GetUpVectorAtSplineInputKey(Key, ESplineCoordinateSpace::World);
							PDI->DrawLine(NewPos, NewPos + NewUpVector * SplineComp->ScaleVisualizationWidth * 0.5f, LineColor, SDPG_Foreground);
							PDI->DrawLine(NewPos, NewPos + NewRightVector * SplineComp->ScaleVisualizationWidth * 0.5f, LineColor, SDPG_Foreground);
							#endif
						}

						OldPos = NewPos;
						OldRightVector = NewRightVector;
						OldScale = NewScale;
					}
				}

				PDI->SetHitProxy(NULL);
			}

			OldKeyPos = NewKeyPos;
			OldKeyRightVector = NewKeyRightVector;
			OldKeyScale = NewKeyScale;
		}
	}
}

void FSplineExComponentVisualizer::ChangeSelectionState(int32 Index, bool bIsCtrlHeld)
{
	if (Index == INDEX_NONE)
	{
		SelectedKeys.Empty();
		LastKeyIndexSelected = INDEX_NONE;
	}
	else if (!bIsCtrlHeld)
	{
		SelectedKeys.Empty();
		SelectedKeys.Add(Index);
		LastKeyIndexSelected = Index;
	}
	else
	{
		// Add or remove from selection if Ctrl is held
		if (SelectedKeys.Contains(Index))
		{
			// If already in selection, toggle it off
			SelectedKeys.Remove(Index);

			if (LastKeyIndexSelected == Index)
			{
				if (SelectedKeys.Num() == 0)
				{
					// Last key selected: clear last key index selected
					LastKeyIndexSelected = INDEX_NONE;
				}
				else
				{
					// Arbitarily set last key index selected to first member of the set (so that it is valid)
					LastKeyIndexSelected = *SelectedKeys.CreateConstIterator();
				}
			}
		}
		else
		{
			// Add to selection
			SelectedKeys.Add(Index);
			LastKeyIndexSelected = Index;
		}
	}
}

bool FSplineExComponentVisualizer::VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click)
{
	if(VisProxy && VisProxy->Component.IsValid())
	{
		const USplineExComponent* SplineComp = CastChecked<const USplineExComponent>(VisProxy->Component.Get());

		SplineCompPropName = GetComponentPropertyName(SplineComp);
		if(SplineCompPropName.IsValid())
		{
			AActor* OldSplineOwningActor = SplineOwningActor.Get();
			SplineOwningActor = SplineComp->GetOwner();

			if (OldSplineOwningActor != SplineOwningActor)
			{
				// Reset selection state if we are selecting a different actor to the one previously selected
				ChangeSelectionState(INDEX_NONE, false);
				SelectedSegmentIndex = INDEX_NONE;
				SelectedTangentHandle = INDEX_NONE;
				SelectedTangentHandleType = ESelectedTangentHandle::None;
			}

			if (VisProxy->IsA(HSplineKeyProxy::StaticGetType()))
			{
				// Control point clicked

				HSplineKeyProxy* KeyProxy = (HSplineKeyProxy*)VisProxy;

				// Modify the selection state, unless right-clicking on an already selected key
				if (Click.GetKey() != EKeys::RightMouseButton || !SelectedKeys.Contains(KeyProxy->KeyIndex))
				{
					ChangeSelectionState(KeyProxy->KeyIndex, InViewportClient->IsCtrlPressed());
				}
				SelectedSegmentIndex = INDEX_NONE;
				SelectedTangentHandle = INDEX_NONE;
				SelectedTangentHandleType = ESelectedTangentHandle::None;

				if (LastKeyIndexSelected == INDEX_NONE)
				{
					SplineOwningActor = nullptr;
					return false;
				}

				CachedRotation = SplineComp->GetQuaternionAtSplinePoint(LastKeyIndexSelected, ESplineCoordinateSpace::World);

				return true;
			}
			else if (VisProxy->IsA(HSplineSegmentProxy::StaticGetType()))
			{
				// Spline segment clicked

				// Divide segment into subsegments and test each subsegment against ray representing click position and camera direction.
				// Closest encounter with the spline determines the spline position.
				const int32 NumSubdivisions = 16;

				HSplineSegmentProxy* SegmentProxy = (HSplineSegmentProxy*)VisProxy;
				ChangeSelectionState(SegmentProxy->SegmentIndex, InViewportClient->IsCtrlPressed());
				SelectedSegmentIndex = SegmentProxy->SegmentIndex;
				SelectedTangentHandle = INDEX_NONE;
				SelectedTangentHandleType = ESelectedTangentHandle::None;

				if (LastKeyIndexSelected == INDEX_NONE)
				{
					SplineOwningActor = nullptr;
					return false;
				}

				CachedRotation = SplineComp->GetQuaternionAtSplinePoint(LastKeyIndexSelected, ESplineCoordinateSpace::World);

				float SubsegmentStartKey = static_cast<float>(SelectedSegmentIndex);
				FVector SubsegmentStart = SplineComp->GetLocationAtSplineInputKey(SubsegmentStartKey, ESplineCoordinateSpace::World);

				float ClosestDistance = TNumericLimits<float>::Max();
				FVector BestLocation = SubsegmentStart;

				for (int32 Step = 1; Step < NumSubdivisions; Step++)
				{
					const float SubsegmentEndKey = SelectedSegmentIndex + Step / static_cast<float>(NumSubdivisions);
					const FVector SubsegmentEnd = SplineComp->GetLocationAtSplineInputKey(SubsegmentEndKey, ESplineCoordinateSpace::World);

					FVector SplineClosest;
					FVector RayClosest;
					FMath::SegmentDistToSegmentSafe(SubsegmentStart, SubsegmentEnd, Click.GetOrigin(), Click.GetOrigin() + Click.GetDirection() * 50000.0f, SplineClosest, RayClosest);

					const float Distance = FVector::DistSquared(SplineClosest, RayClosest);
					if (Distance < ClosestDistance)
					{
						ClosestDistance = Distance;
						BestLocation = SplineClosest;
					}

					SubsegmentStartKey = SubsegmentEndKey;
					SubsegmentStart = SubsegmentEnd;
				}

				SelectedSplinePosition = BestLocation;

				return true;
			}
			else if (VisProxy->IsA(HSplineTangentHandleProxy::StaticGetType()))
			{
				// Tangent handle clicked

				HSplineTangentHandleProxy* KeyProxy = (HSplineTangentHandleProxy*)VisProxy;

				// Note: don't change key selection when a tangent handle is clicked
				SelectedSegmentIndex = INDEX_NONE;
				SelectedTangentHandle = KeyProxy->KeyIndex;
				SelectedTangentHandleType = KeyProxy->bArriveTangent ? ESelectedTangentHandle::Arrive : ESelectedTangentHandle::Leave;

				CachedRotation = SplineComp->GetQuaternionAtSplinePoint(SelectedTangentHandle, ESplineCoordinateSpace::World);

				return true;
			}
		}
		else
		{
			SplineOwningActor = nullptr;
		}
	}

	return false;
}

USplineExComponent* FSplineExComponentVisualizer::GetEditedSplineComponent() const
{
	return Cast<USplineExComponent>(GetComponentFromPropertyName(SplineOwningActor.Get(), SplineCompPropName));
}


bool FSplineExComponentVisualizer::GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const
{
	USplineExComponent* SplineComp = GetEditedSplineComponent();
	if (SplineComp != nullptr)
	{
		const FInterpCurveVector& Position = SplineComp->GetSplinePointsPosition();

		if (SelectedTangentHandle != INDEX_NONE)
		{
			// If tangent handle index is set, use that
			check(SelectedTangentHandle < Position.Points.Num());
			const auto& Point = Position.Points[SelectedTangentHandle];

			check(SelectedTangentHandleType != ESelectedTangentHandle::None);
			if (SelectedTangentHandleType == ESelectedTangentHandle::Leave)
			{
				OutLocation = SplineComp->GetComponentTransform().TransformPosition(Point.OutVal + Point.LeaveTangent);
			}
			else if (SelectedTangentHandleType == ESelectedTangentHandle::Arrive)
			{
				OutLocation = SplineComp->GetComponentTransform().TransformPosition(Point.OutVal - Point.ArriveTangent);
			}

			return true;
		}
		else if (LastKeyIndexSelected != INDEX_NONE)
		{
			// Otherwise use the last key index set
			check(LastKeyIndexSelected < Position.Points.Num());
			check(SelectedKeys.Contains(LastKeyIndexSelected));
			const auto& Point = Position.Points[LastKeyIndexSelected];
			OutLocation = SplineComp->GetComponentTransform().TransformPosition(Point.OutVal);
			return true;
		}
	}

	return false;
}


bool FSplineExComponentVisualizer::GetCustomInputCoordinateSystem(const FEditorViewportClient* ViewportClient, FMatrix& OutMatrix) const
{
	if (ViewportClient->GetWidgetCoordSystemSpace() == COORD_Local || ViewportClient->GetWidgetMode() == FWidget::WM_Rotate)
	{
		USplineExComponent* SplineComp = GetEditedSplineComponent();
		if (SplineComp != nullptr)
		{
			OutMatrix = FRotationMatrix::Make(CachedRotation);
			return true;
		}
	}

	return false;
}


bool FSplineExComponentVisualizer::IsVisualizingArchetype() const
{
	USplineExComponent* SplineComp = GetEditedSplineComponent();
	return (SplineComp && SplineComp->GetOwner() && FActorEditorUtils::IsAPreviewOrInactiveActor(SplineComp->GetOwner()));
}


bool FSplineExComponentVisualizer::HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& DeltaTranslate, FRotator& DeltaRotate, FVector& DeltaScale)
{
	USplineExComponent* SplineComp = GetEditedSplineComponent();
	if (SplineComp != nullptr)
	{
		FInterpCurveVector& SplinePosition = SplineComp->GetSplinePointsPosition();
		FInterpCurveQuat& SplineRotation = SplineComp->GetSplinePointsRotation();
		FInterpCurveVector& SplineScale = SplineComp->GetSplinePointsScale();

		const int32 NumPoints = SplinePosition.Points.Num();

		if (SelectedTangentHandle != INDEX_NONE)
		{
			// When tangent handles are manipulated...

			check(SelectedTangentHandle < NumPoints);

			if (!DeltaTranslate.IsZero())
			{
				check(SelectedTangentHandleType != ESelectedTangentHandle::None);

				SplineComp->Modify();

				FInterpCurvePoint<FVector>& EditedPoint = SplinePosition.Points[SelectedTangentHandle];
				if (SplineComp->bAllowDiscontinuousSpline)
				{
					if (SelectedTangentHandleType == ESelectedTangentHandle::Leave)
					{
						EditedPoint.LeaveTangent += SplineComp->GetComponentTransform().InverseTransformVector(DeltaTranslate);
					}
					else
					{
						EditedPoint.ArriveTangent += SplineComp->GetComponentTransform().InverseTransformVector(-DeltaTranslate);
					}
				}
				else
				{
					const FVector Delta = (SelectedTangentHandleType == ESelectedTangentHandle::Leave) ? DeltaTranslate : -DeltaTranslate;
					const FVector Tangent = EditedPoint.LeaveTangent + SplineComp->GetComponentTransform().InverseTransformVector(Delta);

					EditedPoint.LeaveTangent = Tangent;
					EditedPoint.ArriveTangent = Tangent;
				}

				EditedPoint.InterpMode = CIM_CurveUser;
			}
		}
		else
		{
			// When spline keys are manipulated...

			check(LastKeyIndexSelected != INDEX_NONE);
			check(LastKeyIndexSelected < NumPoints);
			check(SelectedKeys.Num() > 0);

			SplineComp->Modify();

			if (ViewportClient->IsAltPressed() && bAllowDuplication)
			{
				DuplicateKey();

				// Don't duplicate again until we release LMB
				bAllowDuplication = false;
			}

			for (int32 SelectedKeyIndex : SelectedKeys)
			{
				FInterpCurvePoint<FVector>& EditedPoint = SplinePosition.Points[SelectedKeyIndex];
				FInterpCurvePoint<FQuat>& EditedRotPoint = SplineRotation.Points[SelectedKeyIndex];
				FInterpCurvePoint<FVector>& EditedScalePoint = SplineScale.Points[SelectedKeyIndex];

				if (!DeltaTranslate.IsZero())
				{
					// Find key position in world space
					const FVector CurrentWorldPos = SplineComp->GetComponentTransform().TransformPosition(EditedPoint.OutVal);
					// Move in world space
					const FVector NewWorldPos = CurrentWorldPos + DeltaTranslate;
					// Convert back to local space
					EditedPoint.OutVal = SplineComp->GetComponentTransform().InverseTransformPosition(NewWorldPos);
				}

				if (!DeltaRotate.IsZero())
				{
					// Set point tangent as user controlled
					EditedPoint.InterpMode = CIM_CurveUser;

					// Rotate tangent according to delta rotation
					FVector NewTangent = SplineComp->GetComponentTransform().GetRotation().RotateVector(EditedPoint.LeaveTangent); // convert local-space tangent vector to world-space
					NewTangent = DeltaRotate.RotateVector(NewTangent); // apply world-space delta rotation to world-space tangent
					NewTangent = SplineComp->GetComponentTransform().GetRotation().Inverse().RotateVector(NewTangent); // convert world-space tangent vector back into local-space
					EditedPoint.LeaveTangent = NewTangent;
					EditedPoint.ArriveTangent = NewTangent;

					// Rotate spline rotation according to delta rotation
					FQuat NewRot = SplineComp->GetComponentTransform().GetRotation() * EditedRotPoint.OutVal; // convert local-space rotation to world-space
					NewRot = DeltaRotate.Quaternion() * NewRot; // apply world-space rotation
					NewRot = SplineComp->GetComponentTransform().GetRotation().Inverse() * NewRot; // convert world-space rotation to local-space
					EditedRotPoint.OutVal = NewRot;
				}

				if (DeltaScale.X != 0.0f)
				{
					// Set point tangent as user controlled
					EditedPoint.InterpMode = CIM_CurveUser;

					const FVector NewTangent = EditedPoint.LeaveTangent * (1.0f + DeltaScale.X);
					EditedPoint.LeaveTangent = NewTangent;
					EditedPoint.ArriveTangent = NewTangent;
				}

				if (DeltaScale.Y != 0.0f)
				{
					// Scale in Y adjusts the scale spline
					EditedScalePoint.OutVal.Y *= (1.0f + DeltaScale.Y);
				}

				if (DeltaScale.Z != 0.0f)
				{
					// Scale in Z adjusts the scale spline
					EditedScalePoint.OutVal.Z *= (1.0f + DeltaScale.Z);
				}
			}
		}

		SplineComp->UpdateSpline();
		SplineComp->bSplineHasBeenEdited = true;

		NotifyPropertyModified(SplineComp, SplineCurvesProperty);
		return true;
	}

	return false;
}

bool FSplineExComponentVisualizer::HandleInputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	bool bHandled = false;

	if (Key == EKeys::LeftMouseButton && Event == IE_Released)
	{
		USplineExComponent* SplineComp = GetEditedSplineComponent();
		if (SplineComp != nullptr)
		{
			// Recache widget rotation
			int32 Index = SelectedTangentHandle;
			if (Index == INDEX_NONE)
			{
				// If not set, fall back to last key index selected
				Index = LastKeyIndexSelected;
			}

			CachedRotation = SplineComp->GetQuaternionAtSplinePoint(Index, ESplineCoordinateSpace::World);
		}

		// Reset duplication flag on LMB release
		bAllowDuplication = true;
	}

	if (Event == IE_Pressed)
	{
		bHandled = SplineComponentVisualizerActions->ProcessCommandBindings(Key, FSlateApplication::Get().GetModifierKeys(), false);
	}

	return bHandled;
}


void FSplineExComponentVisualizer::EndEditing()
{
	SplineOwningActor = NULL;
	SplineCompPropName.Clear();
	ChangeSelectionState(INDEX_NONE, false);
	SelectedSegmentIndex = INDEX_NONE;
	SelectedTangentHandle = INDEX_NONE;
	SelectedTangentHandleType = ESelectedTangentHandle::None;
}


void FSplineExComponentVisualizer::OnDuplicateKey()
{
	const FScopedTransaction Transaction(LOCTEXT("DuplicateSplinePoint", "Duplicate Spline Point"));
	
	USplineExComponent* SplineComp = GetEditedSplineComponent();
	DuplicateKey();

	SplineComp->UpdateSpline();
	SplineComp->bSplineHasBeenEdited = true;

	NotifyPropertyModified(SplineComp, SplineCurvesProperty);
}


void FSplineExComponentVisualizer::DuplicateKey()
{
	USplineExComponent* SplineComp = GetEditedSplineComponent();
	check(SplineComp != nullptr);
	check(LastKeyIndexSelected != INDEX_NONE);
	check(SelectedKeys.Num() > 0);
	check(SelectedKeys.Contains(LastKeyIndexSelected));

	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	// Get a sorted list of all the selected indices, highest to lowest
	TArray<int32> SelectedKeysSorted;
	for (int32 SelectedKeyIndex : SelectedKeys)
	{
		SelectedKeysSorted.Add(SelectedKeyIndex);
	}
	SelectedKeysSorted.Sort([](int32 A, int32 B) { return A > B; });

	// Insert duplicates into the list, highest index first, so that the lower indices remain the same
	FInterpCurveVector& SplinePosition = SplineComp->SplineCurves.Position;
	FInterpCurveQuat& SplineRotation = SplineComp->SplineCurves.Rotation;
	FInterpCurveVector& SplineScale = SplineComp->SplineCurves.Scale;

	for (int32 SelectedKeyIndex : SelectedKeysSorted)
	{
		// Insert duplicates into arrays.
		// It's necessary to take a copy because copying existing array items by reference isn't allowed (the array may reallocate)
		SplinePosition.Points.Insert(FInterpCurvePoint<FVector>(SplinePosition.Points[SelectedKeyIndex]), SelectedKeyIndex);
		SplineRotation.Points.Insert(FInterpCurvePoint<FQuat>(SplineRotation.Points[SelectedKeyIndex]), SelectedKeyIndex);
		SplineScale.Points.Insert(FInterpCurvePoint<FVector>(SplineScale.Points[SelectedKeyIndex]), SelectedKeyIndex);

		// Adjust input keys of subsequent points
		for (int Index = SelectedKeyIndex + 1; Index < SplinePosition.Points.Num(); Index++)
		{
			SplinePosition.Points[Index].InVal += 1.0f;
			SplineRotation.Points[Index].InVal += 1.0f;
			SplineScale.Points[Index].InVal += 1.0f;
		}
	}

	// Repopulate the selected keys
	SelectedKeys.Empty();
	int32 Offset = SelectedKeysSorted.Num();
	for (int32 SelectedKeyIndex : SelectedKeysSorted)
	{
		SelectedKeys.Add(SelectedKeyIndex + Offset);

		if (LastKeyIndexSelected == SelectedKeyIndex)
		{
			LastKeyIndexSelected += Offset;
		}

		Offset--;
	}

	// Unset tangent handle selection
	SelectedTangentHandle = INDEX_NONE;
	SelectedTangentHandleType = ESelectedTangentHandle::None;

	GEditor->RedrawLevelEditingViewports(true);
}


bool FSplineExComponentVisualizer::CanAddKey() const
{
	USplineExComponent* SplineComp = GetEditedSplineComponent();
	if (SplineComp == nullptr)
	{
		return false;
	}

	const int32 NumPoints = SplineComp->SplineCurves.Position.Points.Num();
	const int32 NumSegments = SplineComp->IsClosedLoop() ? NumPoints : NumPoints - 1;

	return (SelectedSegmentIndex != INDEX_NONE && SelectedSegmentIndex < NumSegments);
}


void FSplineExComponentVisualizer::OnAddKey()
{
	const FScopedTransaction Transaction(LOCTEXT("AddSplinePoint", "Add Spline Point"));
	USplineExComponent* SplineComp = GetEditedSplineComponent();
	check(SplineComp != nullptr);
	check(LastKeyIndexSelected != INDEX_NONE);
	check(SelectedKeys.Num() > 0);
	check(SelectedKeys.Contains(LastKeyIndexSelected));
	check(SelectedTangentHandle == INDEX_NONE);
	check(SelectedTangentHandleType == ESelectedTangentHandle::None);

	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	FInterpCurveVector& SplinePosition = SplineComp->GetSplinePointsPosition();
	FInterpCurveQuat& SplineRotation = SplineComp->GetSplinePointsRotation();
	FInterpCurveVector& SplineScale = SplineComp->GetSplinePointsScale();

	FInterpCurvePoint<FVector> NewPoint(
		SelectedSegmentIndex,
		SplineComp->GetComponentTransform().InverseTransformPosition(SelectedSplinePosition),
		FVector::ZeroVector,
		FVector::ZeroVector,
		CIM_CurveAuto);

	FInterpCurvePoint<FQuat> NewRotPoint(
		SelectedSegmentIndex,
		FQuat::Identity,
		FQuat::Identity,
		FQuat::Identity,
		CIM_CurveAuto);

	FInterpCurvePoint<FVector> NewScalePoint(
		SelectedSegmentIndex,
		FVector(1.0f),
		FVector::ZeroVector,
		FVector::ZeroVector,
		CIM_CurveAuto);

	SplinePosition.Points.Insert(NewPoint, SelectedSegmentIndex + 1);
	SplineRotation.Points.Insert(NewRotPoint, SelectedSegmentIndex + 1);
	SplineScale.Points.Insert(NewScalePoint, SelectedSegmentIndex + 1);

	// Adjust input keys of subsequent points
	for (int Index = SelectedSegmentIndex + 1; Index < SplinePosition.Points.Num(); Index++)
	{
		SplinePosition.Points[Index].InVal += 1.0f;
		SplineRotation.Points[Index].InVal += 1.0f;
		SplineScale.Points[Index].InVal += 1.0f;
	}

	// Set selection to 'next' key
	ChangeSelectionState(SelectedSegmentIndex + 1, false);
	SelectedSegmentIndex = INDEX_NONE;

	SplineComp->UpdateSpline();
	SplineComp->bSplineHasBeenEdited = true;

	NotifyPropertyModified(SplineComp, SplineCurvesProperty);

	CachedRotation = SplineComp->GetQuaternionAtSplinePoint(LastKeyIndexSelected, ESplineCoordinateSpace::World);

	GEditor->RedrawLevelEditingViewports(true);
}


void FSplineExComponentVisualizer::OnDeleteKey()
{
	const FScopedTransaction Transaction(LOCTEXT("DeleteSplinePoint", "Delete Spline Point"));
	USplineExComponent* SplineComp = GetEditedSplineComponent();
	check(SplineComp != nullptr);
	check(LastKeyIndexSelected != INDEX_NONE);
	check(SelectedKeys.Num() > 0);
	check(SelectedKeys.Contains(LastKeyIndexSelected));

	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	// Get a sorted list of all the selected indices, highest to lowest
	TArray<int32> SelectedKeysSorted;
	for (int32 SelectedKeyIndex : SelectedKeys)
	{
		SelectedKeysSorted.Add(SelectedKeyIndex);
	}
	SelectedKeysSorted.Sort([](int32 A, int32 B) { return A > B; });

	// Delete selected keys from list, highest index first
	FInterpCurveVector& SplinePosition = SplineComp->SplineCurves.Position;
	FInterpCurveQuat& SplineRotation = SplineComp->SplineCurves.Rotation;
	FInterpCurveVector& SplineScale = SplineComp->SplineCurves.Scale;

	for (int32 SelectedKeyIndex : SelectedKeysSorted)
	{
		SplinePosition.Points.RemoveAt(SelectedKeyIndex);
		SplineRotation.Points.RemoveAt(SelectedKeyIndex);
		SplineScale.Points.RemoveAt(SelectedKeyIndex);

		for (int Index = SelectedKeyIndex; Index < SplinePosition.Points.Num(); Index++)
		{
			SplinePosition.Points[Index].InVal -= 1.0f;
			SplineRotation.Points[Index].InVal -= 1.0f;
			SplineScale.Points[Index].InVal -= 1.0f;
		}
	}

	// Select first key
	ChangeSelectionState(0, false);
	SelectedSegmentIndex = INDEX_NONE;
	SelectedTangentHandle = INDEX_NONE;
	SelectedTangentHandleType = ESelectedTangentHandle::None;

	SplineComp->UpdateSpline();
	SplineComp->bSplineHasBeenEdited = true;

	NotifyPropertyModified(SplineComp, SplineCurvesProperty);

	CachedRotation = SplineComp->GetQuaternionAtSplinePoint(LastKeyIndexSelected, ESplineCoordinateSpace::World);

	GEditor->RedrawLevelEditingViewports(true);
}


bool FSplineExComponentVisualizer::CanDeleteKey() const
{
	USplineExComponent* SplineComp = GetEditedSplineComponent();
	return (SplineComp != nullptr &&
			SelectedKeys.Num() > 0 &&
			SelectedKeys.Num() != SplineComp->SplineCurves.Position.Points.Num() &&
			LastKeyIndexSelected != INDEX_NONE);
}


bool FSplineExComponentVisualizer::IsKeySelectionValid() const
{
	USplineExComponent* SplineComp = GetEditedSplineComponent();
	return (SplineComp != nullptr &&
			SelectedKeys.Num() > 0 &&
			LastKeyIndexSelected != INDEX_NONE);
}


void FSplineExComponentVisualizer::OnResetToAutomaticTangent(EInterpCurveMode Mode)
{
	USplineExComponent* SplineComp = GetEditedSplineComponent();
	if (SplineComp != nullptr)
	{
		const FScopedTransaction Transaction(LOCTEXT("ResetToAutomaticTangent", "Reset to Automatic Tangent"));

		SplineComp->Modify();
		if (AActor* Owner = SplineComp->GetOwner())
		{
			Owner->Modify();
		}

		for (int32 SelectedKeyIndex : SelectedKeys)
		{
			auto& Point = SplineComp->SplineCurves.Position.Points[SelectedKeyIndex];
			if (Point.IsCurveKey())
			{
				Point.InterpMode = Mode;
			}
		}

		SplineComp->UpdateSpline();
		SplineComp->bSplineHasBeenEdited = true;

		NotifyPropertyModified(SplineComp, SplineCurvesProperty);

		CachedRotation = SplineComp->GetQuaternionAtSplinePoint(LastKeyIndexSelected, ESplineCoordinateSpace::World);
	}
}


bool FSplineExComponentVisualizer::CanResetToAutomaticTangent(EInterpCurveMode Mode) const
{
	USplineExComponent* SplineComp = GetEditedSplineComponent();
	if (SplineComp != nullptr && LastKeyIndexSelected != INDEX_NONE)
	{
		for (int32 SelectedKeyIndex : SelectedKeys)
		{
			const auto& Point = SplineComp->SplineCurves.Position.Points[SelectedKeyIndex];
			if (Point.IsCurveKey() && Point.InterpMode != Mode)
			{
				return true;
			}
		}
	}

	return false;
}


void FSplineExComponentVisualizer::OnSetKeyType(EInterpCurveMode Mode)
{
	USplineExComponent* SplineComp = GetEditedSplineComponent();
	if (SplineComp != nullptr)
	{
		const FScopedTransaction Transaction(LOCTEXT("SetSplinePointType", "Set Spline Point Type"));

		SplineComp->Modify();
		if (AActor* Owner = SplineComp->GetOwner())
		{
			Owner->Modify();
		}

		for (int32 SelectedKeyIndex : SelectedKeys)
		{
			SplineComp->SplineCurves.Position.Points[SelectedKeyIndex].InterpMode = Mode;
		}

		SplineComp->UpdateSpline();
		SplineComp->bSplineHasBeenEdited = true;

		NotifyPropertyModified(SplineComp, SplineCurvesProperty);

		CachedRotation = SplineComp->GetQuaternionAtSplinePoint(LastKeyIndexSelected, ESplineCoordinateSpace::World);
	}
}


bool FSplineExComponentVisualizer::IsKeyTypeSet(EInterpCurveMode Mode) const
{
	if (IsKeySelectionValid())
	{
		USplineExComponent* SplineComp = GetEditedSplineComponent();
		check(SplineComp != nullptr);

		for (int32 SelectedKeyIndex : SelectedKeys)
		{
			const auto& SelectedPoint = SplineComp->SplineCurves.Position.Points[SelectedKeyIndex];
			if ((Mode == CIM_CurveAuto && SelectedPoint.IsCurveKey()) || SelectedPoint.InterpMode == Mode)
			{
				return true;
			}
		}
	}

	return false;
}


void FSplineExComponentVisualizer::OnSetVisualizeRollAndScale()
{
	USplineExComponent* SplineComp = GetEditedSplineComponent();
	check(SplineComp != nullptr);

	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	SplineComp->bShouldVisualizeScale = !SplineComp->bShouldVisualizeScale;

	NotifyPropertyModified(SplineComp, FindField<UProperty>(USplineExComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(USplineExComponent, bShouldVisualizeScale)));

	GEditor->RedrawLevelEditingViewports(true);
}


bool FSplineExComponentVisualizer::IsVisualizingRollAndScale() const
{
	USplineExComponent* SplineComp = GetEditedSplineComponent();
	
	return SplineComp ? SplineComp->bShouldVisualizeScale : false;
}


void FSplineExComponentVisualizer::OnSetDiscontinuousSpline()
{
	USplineExComponent* SplineComp = GetEditedSplineComponent();
	check(SplineComp != nullptr);

	SplineComp->Modify();
	if (AActor* Owner = SplineComp->GetOwner())
	{
		Owner->Modify();
	}

	SplineComp->bAllowDiscontinuousSpline = !SplineComp->bAllowDiscontinuousSpline;

	// If not allowed discontinuous splines, set all ArriveTangents to match LeaveTangents
	if (!SplineComp->bAllowDiscontinuousSpline)
	{
		for (int Index = 0; Index < SplineComp->SplineCurves.Position.Points.Num(); Index++)
		{
			SplineComp->SplineCurves.Position.Points[Index].ArriveTangent = SplineComp->SplineCurves.Position.Points[Index].LeaveTangent;
		}
	}

	TArray<UProperty*> Properties;
	Properties.Add(SplineCurvesProperty);
	Properties.Add(FindField<UProperty>(USplineExComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(USplineExComponent, bAllowDiscontinuousSpline)));
	NotifyPropertiesModified(SplineComp, Properties);

	GEditor->RedrawLevelEditingViewports(true);
}


bool FSplineExComponentVisualizer::IsDiscontinuousSpline() const
{
	USplineExComponent* SplineComp = GetEditedSplineComponent();

	return SplineComp ? SplineComp->bAllowDiscontinuousSpline : false;
}


void FSplineExComponentVisualizer::OnResetToDefault()
{
	USplineExComponent* SplineComp = GetEditedSplineComponent();
	check(SplineComp != nullptr);

	const FScopedTransaction Transaction(LOCTEXT("ResetToDefault", "Reset to Default"));

	SplineComp->Modify();
	if (SplineOwningActor.IsValid())
	{
		SplineOwningActor.Get()->Modify();
	}

	SplineComp->bSplineHasBeenEdited = false;

	// Select first key
	ChangeSelectionState(0, false);
	SelectedSegmentIndex = INDEX_NONE;
	SelectedTangentHandle = INDEX_NONE;
	SelectedTangentHandleType = ESelectedTangentHandle::None;

	if (SplineOwningActor.IsValid())
	{
		SplineOwningActor.Get()->PostEditMove(false);
	}

	GEditor->RedrawLevelEditingViewports(true);
}


bool FSplineExComponentVisualizer::CanResetToDefault() const
{
	USplineExComponent* SplineComp = GetEditedSplineComponent();
	if(SplineComp != nullptr)
    {
        return SplineComp->SplineCurves != CastChecked<USplineExComponent>(SplineComp->GetArchetype())->SplineCurves;
    }
    else
    {
        return false;
    }
}


TSharedPtr<SWidget> FSplineExComponentVisualizer::GenerateContextMenu() const
{
	FMenuBuilder MenuBuilder(true, SplineComponentVisualizerActions);
	{
		MenuBuilder.BeginSection("SplinePointEdit", LOCTEXT("SplinePoint", "Spline Point"));
		{
			if (SelectedSegmentIndex != INDEX_NONE)
			{
				MenuBuilder.AddMenuEntry(FSplineExComponentVisualizerCommands::Get().AddKey);
			}
			else if (LastKeyIndexSelected != INDEX_NONE)
			{
				MenuBuilder.AddMenuEntry(FSplineExComponentVisualizerCommands::Get().DeleteKey);
				MenuBuilder.AddMenuEntry(FSplineExComponentVisualizerCommands::Get().DuplicateKey);

				MenuBuilder.AddSubMenu(
					LOCTEXT("SplinePointType", "Spline Point Type"),
					LOCTEXT("KeyTypeTooltip", "Define the type of the spline point."),
					FNewMenuDelegate::CreateSP(this, &FSplineExComponentVisualizer::GenerateSplinePointTypeSubMenu));

				// Only add the Automatic Tangents submenu if any of the keys is a curve type
				USplineExComponent* SplineComp = GetEditedSplineComponent();
				if (SplineComp != nullptr)
				{
					for (int32 SelectedKeyIndex : SelectedKeys)
					{
						const auto& Point = SplineComp->SplineCurves.Position.Points[SelectedKeyIndex];
						if (Point.IsCurveKey())
						{
							MenuBuilder.AddSubMenu(
								LOCTEXT("ResetToAutomaticTangent", "Reset to Automatic Tangent"),
								LOCTEXT("ResetToAutomaticTangentTooltip", "Reset the spline point tangent to an automatically generated value."),
								FNewMenuDelegate::CreateSP(this, &FSplineExComponentVisualizer::GenerateTangentTypeSubMenu));
							break;
						}
					}
				}
			}
		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("Spline", LOCTEXT("Spline", "Spline"));
		{
			MenuBuilder.AddMenuEntry(FSplineExComponentVisualizerCommands::Get().ResetToDefault);
		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("Visualization", LOCTEXT("Visualization", "Visualization"));
		{
			MenuBuilder.AddMenuEntry(FSplineExComponentVisualizerCommands::Get().VisualizeRollAndScale);
			MenuBuilder.AddMenuEntry(FSplineExComponentVisualizerCommands::Get().DiscontinuousSpline);
		}
		MenuBuilder.EndSection();
	}

	TSharedPtr<SWidget> MenuWidget = MenuBuilder.MakeWidget();
	return MenuWidget;
}


void FSplineExComponentVisualizer::GenerateSplinePointTypeSubMenu(FMenuBuilder& MenuBuilder) const
{
	MenuBuilder.AddMenuEntry(FSplineExComponentVisualizerCommands::Get().SetKeyToCurve);
	MenuBuilder.AddMenuEntry(FSplineExComponentVisualizerCommands::Get().SetKeyToLinear);
	MenuBuilder.AddMenuEntry(FSplineExComponentVisualizerCommands::Get().SetKeyToConstant);
}


void FSplineExComponentVisualizer::GenerateTangentTypeSubMenu(FMenuBuilder& MenuBuilder) const
{
	MenuBuilder.AddMenuEntry(FSplineExComponentVisualizerCommands::Get().ResetToUnclampedTangent);
	MenuBuilder.AddMenuEntry(FSplineExComponentVisualizerCommands::Get().ResetToClampedTangent);
}

#undef LOCTEXT_NAMESPACE
