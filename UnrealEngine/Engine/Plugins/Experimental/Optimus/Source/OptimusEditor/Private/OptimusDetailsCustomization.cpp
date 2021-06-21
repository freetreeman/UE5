// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDetailsCustomization.h"

#include "OptimusEditorStyle.h"
#include "OptimusHLSLSyntaxHighlighter.h"
#include "SOptimusDataTypeSelector.h"

#include "OptimusDataTypeRegistry.h"
#include "Types/OptimusType_ShaderText.h"

#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "DetailWidgetRow.h"
#include "IPropertyTypeCustomization.h"
#include "IDetailChildrenBuilder.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "OptimusDetailCustomization"


TSharedRef<IPropertyTypeCustomization> FOptimusDataTypeRefCustomization::MakeInstance()
{
	return MakeShared<FOptimusDataTypeRefCustomization>();
}


void FOptimusDataTypeRefCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> InPropertyHandle, 
	FDetailWidgetRow& InHeaderRow, 
	IPropertyTypeCustomizationUtils& InCustomizationUtils
	)
{
	EOptimusDataTypeUsageFlags UsageMask = EOptimusDataTypeUsageFlags::Node;
	
	if (InPropertyHandle->HasMetaData(FName(TEXT("UseInResource"))))
	{
		UsageMask |= EOptimusDataTypeUsageFlags::Resource;
	}
	if (InPropertyHandle->HasMetaData(FName(TEXT("UseInVariable"))))
	{
		UsageMask |= EOptimusDataTypeUsageFlags::Variable;
	}

	TypeNameProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusDataTypeRef, TypeName));

	InHeaderRow
	.NameContent()
	[
		InPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SOptimusDataTypeSelector)
		.CurrentDataType(this, &FOptimusDataTypeRefCustomization::GetCurrentDataType)
		.UsageMask(UsageMask)
		.Font(InCustomizationUtils.GetRegularFont())
		.OnDataTypeChanged(this, &FOptimusDataTypeRefCustomization::OnDataTypeChanged)
	];
}


void FOptimusDataTypeRefCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	// FIXME: This doesn't update quite properly. Need a better approach.
	FDetailWidgetRow& DeclarationRow = InChildBuilder.AddCustomRow(LOCTEXT("Declaration", "Declaration"));

	DeclarationRow
	.NameContent()
	[
		InPropertyHandle->CreatePropertyNameWidget(LOCTEXT("Declaration", "Declaration"))
	]
	.ValueContent()
	.HAlign(HAlign_Fill)
	[
		SNew(SBox)
		.MinDesiredWidth(180.0f)
		[
			SNew(SMultiLineEditableTextBox)
			.Text(this, &FOptimusDataTypeRefCustomization::GetDeclarationText)
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", InCustomizationUtils.GetRegularFont().Size))
			.IsReadOnly(true)
		]
	];
}


FOptimusDataTypeHandle FOptimusDataTypeRefCustomization::GetCurrentDataType() const
{
	FName TypeName;
	TypeNameProperty->GetValue(TypeName);
	return FOptimusDataTypeRegistry::Get().FindType(TypeName);
}


void FOptimusDataTypeRefCustomization::OnDataTypeChanged(FOptimusDataTypeHandle InDataType)
{
	CurrentDataType = InDataType;
	TypeNameProperty->SetValue(InDataType.IsValid() ? InDataType->TypeName : NAME_None);
}


FText FOptimusDataTypeRefCustomization::GetDeclarationText() const
{
	FOptimusDataTypeHandle DataType = GetCurrentDataType();

	if (DataType.IsValid() && DataType->ShaderValueType.IsValid())
	{
		const FShaderValueType* ValueType = DataType->ShaderValueType.ValueTypePtr;
		FText Declaration;
		if (ValueType->Type == EShaderFundamentalType::Struct)
		{
			return FText::FromString(ValueType->GetTypeDeclaration());
		}
		else
		{
			return FText::FromString(ValueType->ToString());
		}
	}
	else
	{
		return FText::GetEmpty();
	}
}

// =============================================================================================

// The current tab width for the editor.
static constexpr int32 GTabWidth = 4;


TSharedRef<IPropertyTypeCustomization> FOptimusType_ShaderTextCustomization::MakeInstance()
{
	return MakeShared<FOptimusType_ShaderTextCustomization>();
}


FOptimusType_ShaderTextCustomization::FOptimusType_ShaderTextCustomization() :
	SyntaxHighlighter(FOptimusHLSLSyntaxHighlighter::Create(FOptimusHLSLSyntaxHighlighter::FSyntaxTextStyle())),
	SyntaxHighlighterMain(FOptimusHLSLSyntaxHighlighter::Create(FOptimusHLSLSyntaxHighlighter::FSyntaxTextStyle()))
{
	
}


void FOptimusType_ShaderTextCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> InPropertyHandle, 
	FDetailWidgetRow& InHeaderRow, 
	IPropertyTypeCustomizationUtils& InCustomizationUtils
	)
{
	DeclarationsProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusType_ShaderText, Declarations));
	ShaderTextProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusType_ShaderText, ShaderText));

	HorizontalScrollbar =
	    SNew(SScrollBar)
	        .AlwaysShowScrollbar(true)
	        .Orientation(Orient_Horizontal);

	VerticalScrollbar =
	    SNew(SScrollBar)
			.AlwaysShowScrollbar(true)
	        .Orientation(Orient_Vertical);

	const FTextBlockStyle &TextStyle = FOptimusEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("TextEditor.NormalText");
	const FSlateFontInfo &Font = TextStyle.Font;

	InHeaderRow
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.VAlign(VAlign_Top)
		.AutoHeight()
		[
			InPropertyHandle->CreatePropertyNameWidget()
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SBorder)
			.BorderImage(FOptimusEditorStyle::Get().GetBrush("TextEditor.Border"))
			.BorderBackgroundColor(FLinearColor::Black)
			[
				SNew(SGridPanel)
				.FillColumn(0, 1.0f)
				.FillRow(0, 1.0f)
				+SGridPanel::Slot(0, 0)
				[
					SNew(SScrollBox)
					.Orientation(Orient_Vertical)
					.ExternalScrollbar(VerticalScrollbar)
					+ SScrollBox::Slot()
					[
						SNew(STextBlock)
						.Font(InCustomizationUtils.GetBoldFont())
						.Text(LOCTEXT("OptimusType_ShaderTextCustomization_Decl", "Declarations:"))
						.Margin(FMargin(0, 3, 0, 0))
					]
					+ SScrollBox::Slot()
					[
						SNew(SMultiLineEditableText)
						.Font(Font)
						.TextStyle(&TextStyle)
						.Text(this, &FOptimusType_ShaderTextCustomization::GetDeclarationsText)
						.Marshaller(SyntaxHighlighter)
						.HScrollBar(HorizontalScrollbar)
						.AutoWrapText(false)
						.IsReadOnly(true)
					]
					+ SScrollBox::Slot()
					[
						SNew(SSeparator)
					]
					+ SScrollBox::Slot()
					[
						SNew(STextBlock)
						.Font(InCustomizationUtils.GetBoldFont())
						.Text(LOCTEXT("OptimusType_ShaderTextCustomization_Src", "Compute Kernel Source:"))
						.Margin(FMargin(0, 3, 0, 0))
					]
					+ SScrollBox::Slot()
					[
						SAssignNew(ShaderEditor, SMultiLineEditableText)
						.Font(Font)
						.TextStyle(&TextStyle)
						.Text(this, &FOptimusType_ShaderTextCustomization::GetShaderText)
						.OnTextChanged(this, &FOptimusType_ShaderTextCustomization::OnShaderTextChanged)
						// By default, the Tab key gets routed to "next widget". We want to disable that behaviour.
						.OnIsTypedCharValid_Lambda([](const TCHAR InChar) { return true; })
						.OnKeyCharHandler(this, &FOptimusType_ShaderTextCustomization::OnShaderTextKeyChar)
						.AutoWrapText(false)
						.Marshaller(SyntaxHighlighterMain)
						.HScrollBar(HorizontalScrollbar)
					]
				]
				+SGridPanel::Slot(1, 0)
				[
					VerticalScrollbar.ToSharedRef()
				]
				+SGridPanel::Slot(0, 1)
				[
					HorizontalScrollbar.ToSharedRef()
				]
			]
		]
	];
}


FText FOptimusType_ShaderTextCustomization::GetDeclarationsText() const
{
	FString Preamble;
	DeclarationsProperty->GetValue(Preamble);
	return FText::FromString(Preamble);
}


FText FOptimusType_ShaderTextCustomization::GetShaderText() const
{
	FString ShaderText;
	ShaderTextProperty->GetValue(ShaderText);
	return FText::FromString(ShaderText);
}


void FOptimusType_ShaderTextCustomization::OnShaderTextChanged(const FText& InText)
{
	ShaderTextProperty->SetValue(InText.ToString());
}


FReply FOptimusType_ShaderTextCustomization::OnShaderTextKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent)
{
	if (ShaderEditor->IsTextReadOnly())
	{
		return FReply::Unhandled();
	}

	const TCHAR Character = InCharacterEvent.GetCharacter();
	if (Character == TEXT('\t'))
	{
		// Tab to nearest 4.
		ShaderEditor->InsertTextAtCursor(TEXT("    "));
		return FReply::Handled();
	}
	else if (Character == TEXT('\n'))
	{
		// Figure out if we need to auto-indent.
		FString CurrentLine;
		ShaderEditor->GetCurrentTextLine(CurrentLine);

		// See what the open/close curly brace balance is.
		int32 BraceBalance = 0;
		for (TCHAR Char : CurrentLine)
		{
			BraceBalance += (Char == TEXT('{'));
			BraceBalance -= (Char == TEXT('}'));
		}

		return FReply::Handled();
	}
	else
	{
		// Let SMultiLineEditableText::OnKeyChar handle it.
		return FReply::Unhandled();
	}
}


#undef LOCTEXT_NAMESPACE
