
#include "SplineExEditorPrivatePCH.h"
#include "Components/PrimitiveComponent.h"
#include "Editor/UnrealEdEngine.h"

#include "UnrealEdGlobals.h"

#include "SplineExComponent.h"
#include "SplineExComponentVisualizer.h"
#include "SplineExComponentDetails.h"
#include <PropertyEditorModule.h>

class FSplineExEditor : public IModuleInterface
{	

	/** Array of component class names we have registered, so we know what to unregister afterwards */
	TArray<FName> RegisteredComponentClassNames;
	TSet< FName > RegisteredClassNames;


	virtual void StartupModule() override
	{
		RegisterCustomClassLayout("SplineExComponent", FOnGetDetailCustomizationInstance::CreateStatic(&FSplineExComponentDetails::MakeInstance));
		OnPostEngineInit();
		//FCoreDelegates::OnPostEngineInit.AddRaw(this, &FComponentVisualizersModule::OnPostEngineInit);
	}

	void OnPostEngineInit()
	{
		RegisterComponentVisualizer(USplineExComponent::StaticClass()->GetFName(), MakeShareable(new FSplineExComponentVisualizer));
	}

	virtual void ShutdownModule() override
	{
		if (GUnrealEd != NULL)
		{
			// Iterate over all class names we registered for
			for (FName ClassName : RegisteredComponentClassNames)
			{
				GUnrealEd->UnregisterComponentVisualizer(ClassName);
			}

			FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
			// Unregister all classes customized by name
			for (auto It = RegisteredClassNames.CreateConstIterator(); It; ++It)
			{
				if (It->IsValid())
				{
					PropertyModule.UnregisterCustomClassLayout(*It);
				}
			}
		}


	}

	void RegisterComponentVisualizer(FName ComponentClassName, TSharedPtr<FComponentVisualizer> Visualizer)
	{
		if (GUnrealEd != NULL)
		{
			GUnrealEd->RegisterComponentVisualizer(ComponentClassName, Visualizer);
		}

		RegisteredComponentClassNames.Add(ComponentClassName);

		if (Visualizer.IsValid())
		{
			Visualizer->OnRegister();
		}
	}

	void RegisterCustomClassLayout(FName ClassName, FOnGetDetailCustomizationInstance DetailLayoutDelegate)
	{
		check(ClassName != NAME_None);

		RegisteredClassNames.Add(ClassName);

		static FName PropertyEditor("PropertyEditor");
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(PropertyEditor);
		PropertyModule.RegisterCustomClassLayout(ClassName, DetailLayoutDelegate);
	}
};

IMPLEMENT_MODULE(FSplineExEditor, SplineExEditor)
