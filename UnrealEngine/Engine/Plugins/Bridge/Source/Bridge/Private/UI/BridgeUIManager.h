// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/Docking/SDockTab.h"
#include "SWebBrowser.h"
#include "UI/BrowserBinding.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Docking/TabManager.h"


class FBridgeUIManagerImpl;
class FArguments;
class SCompoundWidget;

class FBridgeUIManagerImpl
{
public:
	void Initialize();
	void Shutdown();
	void HandleBrowserUrlChanged(const FText& Url);
	TSharedPtr<SWebBrowser> WebBrowserWidget;
	TSharedPtr<SWindow> BridgeWindow;
	TSharedPtr<SWindow> DragDropWindow;
	TSharedPtr<SWindow> OverlayWindow;

private:
	void SetupMenuItem();
	void CreateWIndow();
	//TSharedRef<SDockTab> CreateBridgeTab(const FSpawnTabArgs& Args);
	void FillToolbar(FToolBarBuilder& ToolbarBuilder);
	void AddPluginMenu(FMenuBuilder& MenuBuilder);

protected:
	const FText BridgeTabDisplay = FText::FromString("Bridge");
	const FText BridgeToolTip = FText::FromString("Launch Megascans Bridge");
};

class  FBridgeUIManager
{
public:
	static void Initialize();
	static void Shutdown();
	static TSharedPtr<FBridgeUIManagerImpl> Instance;
	static UBrowserBinding* BrowserBinding;
};

