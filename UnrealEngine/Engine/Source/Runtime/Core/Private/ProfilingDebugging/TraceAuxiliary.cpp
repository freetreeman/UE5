// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProfilingDebugging/TraceAuxiliary.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Trace/Trace.h"

#if PLATFORM_WINDOWS
#	include "Windows/AllowWindowsPlatformTypes.h"
#	include <Windows.h>
#	include "Windows/HideWindowsPlatformTypes.h"
#endif

#if !defined(WITH_UNREAL_TRACE_LAUNCH)
#	define WITH_UNREAL_TRACE_LAUNCH (PLATFORM_DESKTOP && !UE_BUILD_SHIPPING && !IS_PROGRAM)
#endif

#include "CoreGlobals.h"
#include "Misc/Paths.h"

#include <atomic>

#if UE_TRACE_ENABLED

#include "BuildSettings.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CString.h"
#include "Misc/DateTime.h"
#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "ProfilingDebugging/PlatformFileTrace.h"
#include "ProfilingDebugging/PlatformEvents.h"
#include "String/ParseTokens.h"
#include "Templates/UnrealTemplate.h"
#include "Trace/Trace.inl"

////////////////////////////////////////////////////////////////////////////////
const TCHAR* GDefaultChannels = TEXT("cpu,gpu,frame,log,bookmark");
const TCHAR* GMemoryChannels = TEXT("memtag,memalloc,callstack,module");

////////////////////////////////////////////////////////////////////////////////
enum class ETraceConnectType
{
	Network,
	File
};

////////////////////////////////////////////////////////////////////////////////
class FTraceAuxiliaryImpl
{
public:
	const TCHAR*			GetDest() const;
	template <class T> void	ReadChannels(T&& Callback) const;
	void					AddChannels(const TCHAR* ChannelList);
	bool					Connect(ETraceConnectType Type, const TCHAR* Parameter);
	bool					Stop();
	void					EnableChannels();
	void					DisableChannels();
	void					SetTruncateFile(bool bTruncateFile);

private:
	enum class EState : uint8
	{
		Stopped,
		Tracing,
	};

	struct FChannel
	{
		FString				Name;
		bool				bActive = false;
	};

	void					AddChannel(const TCHAR* Name);
	void					AddChannels(const TCHAR* Name, bool bResolvePresets);
	void					EnableChannel(FChannel& Channel);
	bool					SendToHost(const TCHAR* Host);
	bool					WriteToFile(const TCHAR* Path=nullptr);

	TMap<uint32, FChannel>	Channels;
	FString					TraceDest;
	EState					State = EState::Stopped;
	bool					bTruncateFile = false;
};

static FTraceAuxiliaryImpl GTraceAuxiliary;

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliaryImpl::AddChannels(const TCHAR* ChannelList)
{
	AddChannels(ChannelList, true);
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliaryImpl::AddChannels(const TCHAR* ChannelList, bool bResolvePresets)
{
	UE::String::ParseTokens(ChannelList, TEXT(","), [this, bResolvePresets] (const FStringView& Token)
	{
		TCHAR Name[80];
		const size_t ChannelNameSize = Token.CopyString(Name, UE_ARRAY_COUNT(Name) - 1);
		Name[ChannelNameSize] = '\0';

		if (bResolvePresets)
		{
			FString Value;
			// Check against hard coded presets
			if(FCString::Stricmp(Name, TEXT("default")) == 0)
			{
				AddChannels(GDefaultChannels, false);
			}
			else if(FCString::Stricmp(Name, TEXT("memory"))== 0)
			{
				AddChannels(GMemoryChannels, false);
			}
			// Check against data driven presets (if available)
			else if (GConfig && GConfig->GetString(TEXT("Trace.ChannelPresets"), Name, Value, GEngineIni))
			{
				AddChannels(*Value, false);
				return;
			}
		}

		AddChannel(Name);
	});
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliaryImpl::AddChannel(const TCHAR* Name)
{
	uint32 Hash = 5381;
	for (const TCHAR* c = Name; *c; ++c)
	{
		uint32 LowerC = *c | 0x20;
        Hash = ((Hash << 5) + Hash) + LowerC;
	}

	if (Channels.Find(Hash) != nullptr)
	{
		return;
	}

	FChannel& Value = Channels.Add(Hash, {});
	Value.Name = Name;

	if (State >= EState::Tracing)
	{
		EnableChannel(Value);
	}
}

////////////////////////////////////////////////////////////////////////////////
bool FTraceAuxiliaryImpl::Connect(ETraceConnectType Type, const TCHAR* Parameter)
{
	// Connect/write to file. But only if we're not already sending/writing
	bool bConnected = UE::Trace::IsTracing();
	if (!bConnected)
	{
		if (Type == ETraceConnectType::Network)
		{
			bConnected = SendToHost(Parameter);
		}

		else if (Type == ETraceConnectType::File)
		{
			bConnected = WriteToFile(Parameter);
		}
	}

	if (!bConnected)
	{
		return false;
	}

	// We're now connected. If we don't appear to have any channels we'll set
	// some defaults for the user. Less futzing.
	if (!Channels.Num())
	{
		AddChannels(GDefaultChannels);
	}

	EnableChannels();

	State = EState::Tracing;
	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool FTraceAuxiliaryImpl::Stop()
{
	if (!UE::Trace::Stop())
	{
		return false;
	}

	DisableChannels();
	State = EState::Stopped;
	TraceDest.Reset();
	return true;
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliaryImpl::EnableChannel(FChannel& Channel)
{
	if (Channel.bActive)
	{
		return;
	}

	// Channel names have been provided by the user and may not exist yet. As
	// we want to maintain bActive accurately (channels toggles are reference
	// counted), we will first check Trace knows of the channel.
	if (!UE::Trace::IsChannel(*Channel.Name))
	{
		return;
	}

	EPlatformEvent Event = PlatformEvents_GetEvent(Channel.Name);
	if (Event != EPlatformEvent::None)
	{
		PlatformEvents_Enable(Event);
	}

	UE::Trace::ToggleChannel(*Channel.Name, true);
	Channel.bActive = true;
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliaryImpl::EnableChannels()
{
	for (auto& ChannelPair : Channels)
	{
		EnableChannel(ChannelPair.Value);
	}
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliaryImpl::DisableChannels()
{
	for (auto& ChannelPair : Channels)
	{
		FChannel& Channel = ChannelPair.Value;
		if (Channel.bActive)
		{
			UE::Trace::ToggleChannel(*Channel.Name, false);
			Channel.bActive = false;

			EPlatformEvent Event = PlatformEvents_GetEvent(Channel.Name);
			if (Event != EPlatformEvent::None)
			{
				PlatformEvents_Disable(Event);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliaryImpl::SetTruncateFile(bool bNewTruncateFileState)
{
	bTruncateFile = bNewTruncateFileState;
}

////////////////////////////////////////////////////////////////////////////////
bool FTraceAuxiliaryImpl::SendToHost(const TCHAR* Host)
{
	if (!UE::Trace::SendTo(Host))
	{
		UE_LOG(LogCore, Warning, TEXT("Unable to trace to host '%s'"), Host);
		return false;
	}

	TraceDest = Host;
	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool FTraceAuxiliaryImpl::WriteToFile(const TCHAR* Path)
{
	if (Path == nullptr || *Path == '\0')
	{
		FString Name = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S.utrace"));
		return WriteToFile(*Name);
	}

	FString WritePath;

	// If there's no slash in the path, we'll put it in the profiling directory
	if (FCString::Strchr(Path, '\\') == nullptr && FCString::Strchr(Path, '/') == nullptr)
	{
		WritePath = FPaths::ProfilingDir();
		WritePath += Path;
	}
	else
	{
		WritePath = Path;
	}

	// The user may not have provided a suitable extension
	if (!WritePath.EndsWith(".utrace"))
	{
		WritePath += ".utrace";
	}

	IFileManager& FileManager = IFileManager::Get();

	// Ensure we can write the trace file appropriately
	FString WriteDir = FPaths::GetPath(WritePath);
	if (!FileManager.MakeDirectory(*WriteDir, true))
	{
		UE_LOG(LogCore, Warning, TEXT("Failed to create directory '%s'"), *WriteDir);
		return false;
	}

	if (!bTruncateFile && FileManager.FileExists(*WritePath))
	{
		UE_LOG(LogCore, Warning, TEXT("Trace file '%s' already exists"), *WritePath);
		return false;
	}

	// Finally, tell trace to write the trace to a file.
	FString NativePath = FileManager.ConvertToAbsolutePathForExternalAppForWrite(*WritePath);
	if (!UE::Trace::WriteTo(*NativePath))
	{
		UE_LOG(LogCore, Warning, TEXT("Unable to trace to file '%s'"), *WritePath);
		return false;
	}

	TraceDest = MoveTemp(NativePath);
	return true;
}

////////////////////////////////////////////////////////////////////////////////
const TCHAR* FTraceAuxiliaryImpl::GetDest() const
{
	return *TraceDest;
}

////////////////////////////////////////////////////////////////////////////////
template <class T>
void FTraceAuxiliaryImpl::ReadChannels(T&& Callback) const
{
	for (const auto& ChannelPair : Channels)
	{
		Callback(*(ChannelPair.Value.Name));
	}
}



////////////////////////////////////////////////////////////////////////////////
static void TraceAuxiliaryConnectEpilogue()
{
	// It is possible that something outside of TraceAux's world view has called
	// UE::Trace::SendTo/WriteTo(). A plugin that has created its own store for
	// example. There's not really much that can be done about that here (tracing
	// is singular within a process. We can at least detect the obvious case and
	// inform the user.
	const TCHAR* TraceDest = GTraceAuxiliary.GetDest();
	if (TraceDest[0] == '\0')
	{
		UE_LOG(LogConsoleResponse, Warning, TEXT("Trace system already in use by a plugin or -trace*=... argument. Use 'Trace.Stop' first."));
		return;
	}

	// Give the user some feedback that everything's underway.
	FString Channels;
	GTraceAuxiliary.ReadChannels([&Channels] (const TCHAR* Channel)
	{
		if (Channels.Len())
		{
			Channels += TEXT(",");
		}

		Channels += Channel;
	});
	UE_LOG(LogConsoleResponse, Log, TEXT("Tracing to: %s"), TraceDest);
	UE_LOG(LogConsoleResponse, Log, TEXT("Trace channels: %s"), *Channels);
}

////////////////////////////////////////////////////////////////////////////////
static void TraceAuxiliarySend(const TArray<FString>& Args)
{
	if (Args.Num() < 1)
	{
		UE_LOG(LogConsoleResponse, Warning, TEXT("No host name given; Trace.Send <Host> [ChannelSet]"));
		return;
	}

	if (Args.Num() > 1)
	{
		GTraceAuxiliary.AddChannels(*(Args[1]));
	}

	if (!GTraceAuxiliary.Connect(ETraceConnectType::Network, *Args[0]))
	{
		UE_LOG(LogConsoleResponse, Warning, TEXT("Failed to start tracing to '%s'"), *Args[0]);
		return;
	}

	TraceAuxiliaryConnectEpilogue();
}

////////////////////////////////////////////////////////////////////////////////
static void TraceAuxiliaryStart(const TArray<FString>& Args)
{
	if (Args.Num() > 0)
	{
		GTraceAuxiliary.AddChannels(*(Args[0]));
	}

	if (!GTraceAuxiliary.Connect(ETraceConnectType::File, nullptr))
	{
		UE_LOG(LogConsoleResponse, Warning, TEXT("Failed to start tracing to a file"));
		return;
	}

	TraceAuxiliaryConnectEpilogue();
}

////////////////////////////////////////////////////////////////////////////////
static void TraceAuxiliaryStop()
{
	UE_LOG(LogConsoleResponse, Log, TEXT("Tracing stopped."));
	GTraceAuxiliary.Stop();
}

////////////////////////////////////////////////////////////////////////////////
static void TraceAuxiliaryPause()
{
	UE_LOG(LogConsoleResponse, Log, TEXT("Tracing paused"));
	GTraceAuxiliary.DisableChannels();
}

////////////////////////////////////////////////////////////////////////////////
static void TraceAuxiliaryResume()
{
	UE_LOG(LogConsoleResponse, Log, TEXT("Tracing resumed"));
	GTraceAuxiliary.EnableChannels();
}

////////////////////////////////////////////////////////////////////////////////
static FAutoConsoleCommand TraceAuxiliarySendCmd(
	TEXT("Trace.Send"),
	TEXT("Send trace data to the trace store; Trace.Send <Host> [ChannelSet]"),
	FConsoleCommandWithArgsDelegate::CreateStatic(TraceAuxiliarySend)
);

////////////////////////////////////////////////////////////////////////////////
static FAutoConsoleCommand TraceAuxiliaryStartCmd(
	TEXT("Trace.Start"),
	TEXT(
		"Begin tracing profiling events to a file; Trace.Start [ChannelSet]"
		" where ChannelSet is either comma-separated list of trace channels, a Config/Trace.ChannelPresets key, or optional."
	),
	FConsoleCommandWithArgsDelegate::CreateStatic(TraceAuxiliaryStart)
);

////////////////////////////////////////////////////////////////////////////////
static FAutoConsoleCommand TraceAuxiliaryStopCmd(
	TEXT("Trace.Stop"),
	TEXT("Stops tracing profiling events"),
	FConsoleCommandDelegate::CreateStatic(TraceAuxiliaryStop)
);

////////////////////////////////////////////////////////////////////////////////
static FAutoConsoleCommand TraceAuxiliaryPauseCmd(
	TEXT("Trace.Pause"),
	TEXT("Pauses all trace channels currently sending events"),
	FConsoleCommandDelegate::CreateStatic(TraceAuxiliaryPause)
);

////////////////////////////////////////////////////////////////////////////////
static FAutoConsoleCommand TraceAuxiliaryResumeCmd(
	TEXT("Trace.Resume"),
	TEXT("Resume tracing that was previously paused"),
	FConsoleCommandDelegate::CreateStatic(TraceAuxiliaryResume)
);

#endif // UE_TRACE_ENABLED



#if WITH_UNREAL_TRACE_LAUNCH
////////////////////////////////////////////////////////////////////////////////
static std::atomic<int32> GUnrealTraceLanuched; // = 0;

////////////////////////////////////////////////////////////////////////////////
#if PLATFORM_WINDOWS
static void LaunchUnrealTraceInternal()
{
	if (GUnrealTraceLanuched.load(std::memory_order_relaxed))
	{
		UE_LOG(LogCore, Log, TEXT("UnrealTraceServer: Trace store already started"));
		return;
	}

	FString BinPath = "\"";
	BinPath += FPaths::EngineDir();
	BinPath += "/Binaries/Win64/UnrealTraceServer.exe\"";

	uint32 Flags = 0x0100'0000; // CREATE_BREAKAWAY_FROM_JOB
	STARTUPINFOW StartupInfo = { sizeof(STARTUPINFOW) };
	PROCESS_INFORMATION ProcessInfo = {};
	BOOL bOk = CreateProcessW(nullptr, LPWSTR(*BinPath), nullptr, nullptr,
		false, Flags, nullptr, nullptr, &StartupInfo, &ProcessInfo);

	if (!bOk)
	{
		UE_LOG(LogCore, Warning, TEXT("UnrealTraceServer: Unable to launch the trace store from '%s' (%08x)"), *BinPath, GetLastError());
		return;
	}

	if (WaitForSingleObject(ProcessInfo.hProcess, 5000) == WAIT_TIMEOUT)
	{
		UE_LOG(LogCore, Warning, TEXT("UnrealTraceServer: Timed out waiting for the trace store to start"));
	}
	else
	{
		DWORD ExitCode = 0x0000'a9e0;
		GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
		if (ExitCode)
		{
			UE_LOG(LogCore, Warning, TEXT("UnrealTraceServer: Trace store returned an error (%08x)"), ExitCode);
		}
		else
		{
			UE_LOG(LogCore, Log, TEXT("UnrealTraceServer: Trace store launch successful"));
			GUnrealTraceLanuched.fetch_add(1, std::memory_order_relaxed);
		}
	}

	CloseHandle(ProcessInfo.hProcess);
	CloseHandle(ProcessInfo.hThread);
}
#endif // PLATFORM_WINDOWS

////////////////////////////////////////////////////////////////////////////////
#if PLATFORM_UNIX || PLATFORM_MAC
static void LaunchUnrealTraceInternal()
{
	/* nop */
}
#endif // PLATFORM_UNIX/MAC
#endif // WITH_UNREAL_TRACE_LAUNCH



////////////////////////////////////////////////////////////////////////////////
UE_TRACE_EVENT_BEGIN(Diagnostics, Session2, NoSync|Important)
	UE_TRACE_EVENT_FIELD(UE::Trace::AnsiString, Platform)
	UE_TRACE_EVENT_FIELD(UE::Trace::AnsiString, AppName)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, CommandLine)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, Branch)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, BuildVersion)
	UE_TRACE_EVENT_FIELD(uint32, Changelist)
	UE_TRACE_EVENT_FIELD(uint8, ConfigurationType)
	UE_TRACE_EVENT_FIELD(uint8, TargetType)
UE_TRACE_EVENT_END()

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliary::Initialize(const TCHAR* CommandLine)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FTraceAux_Init);

#if WITH_UNREAL_TRACE_LAUNCH
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FTraceAux_LaunchUnrealTrace);
		LaunchUnrealTraceInternal();
	}
#endif

#if UE_TRACE_ENABLED
	// Trace out information about this session. This is done before initialisation
	// so that it is always sent (all channels are enabled prior to initialisation)
	const TCHAR* BranchName = BuildSettings::GetBranchName();
	const TCHAR* BuildVersion = BuildSettings::GetBuildVersion();
	constexpr uint32 PlatformLen = UE_ARRAY_COUNT(PREPROCESSOR_TO_STRING(UBT_COMPILED_PLATFORM)) - 1;
	constexpr uint32 AppNameLen = UE_ARRAY_COUNT(UE_APP_NAME) - 1;
	const uint32 CommandLineLen = FCString::Strlen(CommandLine);
	const uint32 BranchNameLen = FCString::Strlen(BranchName);
	const uint32 BuildVersionLen = FCString::Strlen(BuildVersion);
	uint32 DataSize =
		(PlatformLen * sizeof(ANSICHAR)) +
		(AppNameLen * sizeof(ANSICHAR)) +
		(CommandLineLen * sizeof(TCHAR)) +
		(BranchNameLen * sizeof(TCHAR)) +
		(BuildVersionLen * sizeof(TCHAR));
	UE_TRACE_LOG(Diagnostics, Session2, UE::Trace::TraceLogChannel, DataSize)
		<< Session2.Platform(PREPROCESSOR_TO_STRING(UBT_COMPILED_PLATFORM), PlatformLen)
		<< Session2.AppName(UE_APP_NAME, AppNameLen)
		<< Session2.CommandLine(CommandLine, CommandLineLen)
		<< Session2.Branch(BranchName, BranchNameLen)
		<< Session2.BuildVersion(BuildVersion, BuildVersionLen)
		<< Session2.Changelist(BuildSettings::GetCurrentChangelist())
		<< Session2.ConfigurationType(uint8(FApp::GetBuildConfiguration()))
		<< Session2.TargetType(uint8(FApp::GetBuildTargetType()));

	// Initialize Trace
	UE::Trace::FInitializeDesc Desc;
	Desc.bUseWorkerThread = FPlatformProcess::SupportsMultithreading();
	if (FParse::Value(CommandLine, TEXT("-tracetailmb="), Desc.TailSizeBytes))
	{
		Desc.TailSizeBytes <<= 20;
	}
	UE::Trace::Initialize(Desc);

	// By default use 1msec for stack sampling interval
	uint32 Microseconds = 1000;
	FParse::Value(CommandLine, TEXT("-samplinginterval="), Microseconds);
	PlatformEvents_Init(Microseconds);

	FCoreDelegates::OnEndFrame.AddStatic(UE::Trace::Update);
	FModuleManager::Get().OnModulesChanged().AddLambda([](FName Name, EModuleChangeReason Reason)
	{
		if (Reason == EModuleChangeReason::ModuleLoaded)
		{
			GTraceAuxiliary.EnableChannels();
		}
	});

	// Extract an explicit channel set from the command line.
	FString Parameter;
	if (FParse::Value(CommandLine, TEXT("-trace="), Parameter, false))
	{
		GTraceAuxiliary.AddChannels(*Parameter);
		GTraceAuxiliary.EnableChannels();
	}

	// Attempt to send trace data somewhere from the command line
	if (FParse::Value(CommandLine, TEXT("-tracehost="), Parameter))
	{
		GTraceAuxiliary.Connect(ETraceConnectType::Network, *Parameter);
	}
	else if (FParse::Value(CommandLine, TEXT("-tracefile="), Parameter))
	{
		GTraceAuxiliary.SetTruncateFile(FParse::Param(CommandLine, TEXT("tracefiletrunc")));
		GTraceAuxiliary.Connect(ETraceConnectType::File, *Parameter);
	}
	else if (FParse::Param(CommandLine, TEXT("tracefile")))
	{
		GTraceAuxiliary.Connect(ETraceConnectType::File, nullptr);
	}

	UE::Trace::ThreadRegister(TEXT("GameThread"), FPlatformTLS::GetCurrentThreadId(), -1);
#endif
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliary::InitializePresets(const TCHAR* CommandLine)
{
#if UE_TRACE_ENABLED
	// Second pass over trace arguments, this time to allow config defined presets
	// to be applied.
	FString Parameter;
	if (FParse::Value(CommandLine, TEXT("-trace="), Parameter, false))
	{
		GTraceAuxiliary.AddChannels(*Parameter);
		GTraceAuxiliary.EnableChannels();
	}
#endif 
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliary::Shutdown()
{
#if UE_TRACE_ENABLED
	// make sure all platform event functionality has shut down as on some
	// platforms it impacts whole system, even if application has terminated
	PlatformEvents_Stop();
#endif
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliary::EnableChannels()
{
#if UE_TRACE_ENABLED
	GTraceAuxiliary.EnableChannels();
#endif
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliary::TryAutoConnect()
{
#if UE_TRACE_ENABLED
	#if PLATFORM_WINDOWS
	// If we can detect a named event then we can try and auto-connect to UnrealInsights.
	HANDLE KnownEvent = ::OpenEvent(EVENT_ALL_ACCESS, false, TEXT("Local\\UnrealInsightsRecorder"));
	if (KnownEvent != nullptr)
	{
		GTraceAuxiliary.Connect(ETraceConnectType::Network, TEXT("127.0.0.1"));
		::CloseHandle(KnownEvent);
	}
	#endif
#endif
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliary::LaunchUnrealTrace()
{
#if WITH_UNREAL_TRACE_LAUNCH
	LaunchUnrealTraceInternal();
#endif
}
