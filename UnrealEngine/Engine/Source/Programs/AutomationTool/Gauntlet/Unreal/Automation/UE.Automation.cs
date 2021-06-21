﻿// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using Gauntlet;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using UnrealBuildTool;

namespace UE
{
	/// <summary>
	/// Define a Config class for UE.EditorAutomation and UE.TargetAutomation with the
	/// options that can be used
	/// </summary>
	public class AutomationTestConfig : UnrealTestConfiguration
	{
		/// <summary>
		/// Run with d3d12 RHI
		/// </summary>
		[AutoParam]
		public bool D3D12 = false;

		/// <summary>
		/// Run with Nvidia cards for raytracing support
		/// </summary>
		[AutoParam]
		public bool PreferNvidia = false;

		/// <summary>
		/// Run forcing raytracing 
		/// </summary>
		[AutoParam]
		public bool RayTracing = false;

		/// <summary>
		/// Run forcing raytracing 
		/// </summary>
		[AutoParam]
		public bool StompMalloc = false;

		/// <summary>
		/// Run with d3ddebug
		/// </summary>
		[AutoParam]
		public bool D3DDebug = false;

		/// <summary>
		/// Disable capturing frame trace for image based tests
		/// </summary>
		[AutoParam]
		public bool DisableFrameTraceCapture = true;

		/// <summary>
		/// Filter or groups of tests to apply
		/// </summary>
		[AutoParam]
		public string RunTest = "";
		
		/// <summary>
		/// Absolute or project relative path to write an automation report to.
		/// </summary>
		[AutoParam]
		public string ReportExportPath = "";

		/// <summary>
		/// Path the report can be found at
		/// </summary>
		[AutoParam]
		public string ReportURL = "";

		/// <summary>
		/// Absolute or project relative directory path to write automation telemetry outputs.
		/// </summary>
		[AutoParam]
		public string TelemetryDirectory = "";

		/// <summary>
		/// Use Simple Horde Report instead of Unreal Automated Tests
		/// </summary>
		[AutoParam]
		public virtual bool SimpleHordeReport { get; set; } = false;

		/// <summary>
		/// Validate DDC during tests
		/// </summary>
		[AutoParam]
		public bool VerifyDDC = false;

		/// <summary>
		/// Validate DDC during tests
		/// </summary>
		[AutoParam]
		public string DDC = "";

		/// <summary>
		/// Used for having the editor and any client communicate
		/// </summary>
		public string SessionID = Guid.NewGuid().ToString();


		/// <summary>
		/// Implement how the settings above are applied to different roles in the session. This is the main avenue for converting options
		/// into command line parameters for the different roles
		/// </summary>
		/// <param name="AppConfig"></param>
		/// <param name="ConfigRole"></param>
		/// <param name="OtherRoles"></param>
		public override void ApplyToConfig(UnrealAppConfig AppConfig, UnrealSessionRole ConfigRole, IEnumerable<UnrealSessionRole> OtherRoles)
		{
			base.ApplyToConfig(AppConfig, ConfigRole, OtherRoles);

			// The "RunTest" argument is required since it is what is passed to the editor to specify which tests to execute
			if (string.IsNullOrEmpty(RunTest))
			{
				throw new AutomationException("No AutomationTest argument specified. Use -RunTest=\"Group:AI\", -RunTest=\"Project\", -RunTest=\"Navigation.Landscape Ramp Test\" etc.");
			}

			// Are we writing out info for Horde?
			if (WriteTestResultsForHorde)
			{
				if (string.IsNullOrEmpty(ReportExportPath))
				{
					ReportExportPath = Path.Combine(Globals.TempDir, "TestReport");
				}
				if (string.IsNullOrEmpty(HordeTestDataPath))
				{
					HordeTestDataPath = HordeReport.DefaultTestDataDir;
				}
				if (string.IsNullOrEmpty(HordeArtifactPath))
				{
					HordeArtifactPath = HordeReport.DefaultArtifactsDir;
				}
			}

			// Setup commandline for telemetry outputs
			if (!string.IsNullOrEmpty(PublishTelemetryTo) || !string.IsNullOrEmpty(TelemetryDirectory))
			{
				if (string.IsNullOrEmpty(TelemetryDirectory))
				{
					TelemetryDirectory = Path.Combine(Globals.TempDir, "Telemetry");
				}
				if (Directory.Exists(TelemetryDirectory))
				{
					// clean any previous data
					Directory.Delete(TelemetryDirectory, true);
				}
				AppConfig.CommandLine += string.Format("-TelemetryDirectory=\"{0}\"", TelemetryDirectory);
			}

			// Arguments for writing out the report and providing a URL where it can be viewed
			string ReportArgs = string.IsNullOrEmpty(ReportExportPath) ? "" : string.Format("-ReportExportPath=\"{0}\"", ReportExportPath);

			if (!string.IsNullOrEmpty(ReportURL))
			{
				ReportArgs += string.Format("-ReportURL=\"{0}\"", ReportURL);
			}

			string AutomationTestArgument = string.Format("RunTest {0};", RunTest);

			// if this is not attended then we'll quit the editor after the tests and disable any user interactions
			if (this.Attended == false)
			{
				AutomationTestArgument += "Quit;";
				AppConfig.CommandLine += " -unattended";
			}

			// If there's only one role and it's the editor then tests are running under the editor with no target
			if (ConfigRole.RoleType == UnrealTargetRole.Editor && OtherRoles.Any() == false)
			{ 
				AppConfig.CommandLine += string.Format(" {0} -ExecCmds=\"Automation {1}\"", ReportArgs, AutomationTestArgument);		
			}
			else
			{
				// If the test isnt using the editor for both roles then pass the IP of the editor (us) to the client
				string HostIP = UnrealHelpers.GetHostIpAddress();

				if (ConfigRole.RoleType.IsClient())
				{
					// have the client list the tests it knows about. useful for troubleshooting discrepencies
					AppConfig.CommandLine += string.Format(" -sessionid={0} -messaging -log -TcpMessagingConnect={1}:6666 -ExecCmds=\"Automation list\"", SessionID, HostIP);
				}
				else if (ConfigRole.RoleType.IsEditor())
				{
					AppConfig.CommandLine += string.Format(" -ExecCmds=\"Automation StartRemoteSession {0}; {1}\" -TcpMessagingListen={2}:6666 -multihome={3} {4}", SessionID, AutomationTestArgument, HostIP, HostIP, ReportArgs);
				}
			}

			if (DisableFrameTraceCapture || RayTracing)
			{
				AppConfig.CommandLine += " -DisableFrameTraceCapture";
			}

			if (RayTracing)
			{
				AppConfig.CommandLine += " -dpcvars=r.RayTracing=1,r.SkinCache.CompileShaders=1,AutomationAllowFrameTraceCapture=0";
			}

			// Options specific to windows
			if (ConfigRole.Platform == UnrealTargetPlatform.Win64)
			{
				if (PreferNvidia)
				{
					AppConfig.CommandLine += " -preferNvidia";
				}

				if (D3D12)
				{
					AppConfig.CommandLine += " -d3d12";
				}

				if (D3DDebug)
				{
					AppConfig.CommandLine += " -d3ddebug";
				}

				if (StompMalloc)
				{
					AppConfig.CommandLine += " -stompmalloc";
				}
			}

			// Options specific to roles running under the editor
			if (ConfigRole.RoleType.UsesEditor())
			{
				if (VerifyDDC)
				{
					AppConfig.CommandLine += " -VerifyDDC";
				}

				if (!string.IsNullOrEmpty(DDC))
				{
					AppConfig.CommandLine += string.Format(" -ddc={0}", DDC);
				}
			}
		}
	}

	/// <summary>
	/// Implements a node that runs Unreal automation tests using the editor. The primary argument is "RunTest". E.g
	/// RunUnreal -test=UE.EditorAutomation -RunTest="Group:Animation"
	/// </summary>
	public class EditorAutomation : AutomationNodeBase<AutomationTestConfig>
	{
		public EditorAutomation(Gauntlet.UnrealTestContext InContext)
			: base(InContext)
		{

		}

		/// <summary>
		/// Define the configuration for this test. Most options are applied by the test config above
		/// </summary>
		/// <returns></returns>
		public override AutomationTestConfig GetConfiguration()
		{
			AutomationTestConfig Config = base.GetConfiguration();

			// Tests in the editor only require a single role
			UnrealTestRole EditorRole = Config.RequireRole(UnrealTargetRole.Editor);
			EditorRole.CommandLineParams.AddRawCommandline("-NoWatchdog -stdout -FORCELOGFLUSH -CrashForUAT -log");

			return Config;
		}
	}

	/// <summary>
	/// Implements a node that runs Unreal automation tests on a target, monitored by an editor. The primary argument is "RunTest". E.g
	/// RunUnreal -test=UE.EditorAutomation -RunTest="Group:Animation"
	/// </summary>
	public class TargetAutomation : AutomationNodeBase<AutomationTestConfig>
	{
		public TargetAutomation(Gauntlet.UnrealTestContext InContext)
			: base(InContext)
		{
		}

		/// <summary>
		/// Define the configuration for this test. Most options are applied by the test config above
		/// </summary>
		/// <returns></returns>
		public override AutomationTestConfig GetConfiguration()
		{
			AutomationTestConfig Config = base.GetConfiguration();

			// Target tests require an editor which hosts the process
			UnrealTestRole EditorRole = Config.RequireRole(UnrealTargetRole.Editor);
			EditorRole.CommandLineParams.AddRawCommandline("-NoWatchdog -stdout -FORCELOGFLUSH -CrashForUAT -log");

			if (Config.Attended == false)
			{
				// if this is unattended we don't need the UI ( this also alows a wider range of machines to run the test under CIS)
				EditorRole.CommandLineParams.Add("nullrhi");
			}

			// target tests also require a client
			Config.RequireRole(UnrealTargetRole.Client);
			return Config;
		}
	}

	/// <summary>
	/// Base class for automation tests. Most of the magic is in here with the Editor/Target forms simply defining the roles
	/// </summary>
	public abstract class AutomationNodeBase<TConfigClass> : UnrealTestNode<TConfigClass>
		where TConfigClass : UnrealTestConfiguration, new()
	{
		// used to track stdout from the processes 
		private int LastAutomationEntryCount = 0;

		private DateTime LastAutomationEntryTime = DateTime.MinValue;

		public AutomationNodeBase(Gauntlet.UnrealTestContext InContext)
			: base(InContext)
		{
		}

	
		/// <summary>
		/// Override our name to include the filter we're testing
		/// </summary>
		public override string Name
		{
			get
			{
				string BaseName = base.Name;

				var Config = GetConfiguration();
				if (Config is AutomationTestConfig)
				{
					var AutomationConfig = Config as AutomationTestConfig;
					if (!string.IsNullOrEmpty(AutomationConfig.RunTest))
					{
						BaseName += string.Format("(RunTest={0})", AutomationConfig.RunTest);
					}
				}

				return BaseName;
			}
		}

		/// <summary>
		/// Override TickTest to log interesting things and make sure nothing has stalled
		/// </summary>
		public override void TickTest()
		{
			const float IdleTimeout = 30 * 60;

			List<string> ChannelEntries = new List<string>();

			// We are primarily interested in what the editor is doing
			var AppInstance = TestInstance.EditorApp;

			UnrealLogParser Parser = new UnrealLogParser(AppInstance.StdOut);
			ChannelEntries.AddRange(Parser.GetEditorBusyChannels());

			// Any new entries?
			if (ChannelEntries.Count > LastAutomationEntryCount)
			{
				// log new entries so people have something to look at
				ChannelEntries.Skip(LastAutomationEntryCount).ToList().ForEach(S => Log.Info("{0}", S));
				LastAutomationEntryTime = DateTime.Now;
				LastAutomationEntryCount = ChannelEntries.Count;
			}
			else
			{
				// Check for timeouts
				if (LastAutomationEntryTime == DateTime.MinValue)
				{
					LastAutomationEntryTime = DateTime.Now;
				}

				double ElapsedTime = (DateTime.Now - LastAutomationEntryTime).TotalSeconds;

				// Check for timeout
				if (ElapsedTime > IdleTimeout)
				{
					Log.Error("No activity observed in last {0:0.00} minutes. Aborting test", IdleTimeout / 60);
					MarkTestComplete();
					SetUnrealTestResult(TestResult.TimedOut);
				}
			}

			base.TickTest();
		}

		/// <summary>
		/// Override GetExitCodeAndReason to provide additional checking of success / failure based on what occurred
		/// </summary>
		/// <param name="InArtifacts"></param>
		/// <param name="ExitReason"></param>
		/// <returns></returns>
		protected override UnrealProcessResult GetExitCodeAndReason(StopReason InReason, UnrealLog InLog, UnrealRoleArtifacts InArtifacts, out string ExitReason, out int ExitCode)
		{
			UnrealProcessResult UnrealResult = base.GetExitCodeAndReason(InReason, InLog, InArtifacts, out ExitReason, out ExitCode);

			// The editor is an additional arbiter of success
			if (InArtifacts.SessionRole.RoleType == UnrealTargetRole.Editor 
				&& InLog.HasAbnormalExit == false)
			{
				// if no fatal errors, check test results
				if (InLog.FatalError == null)
				{
					AutomationLogParser Parser = new AutomationLogParser(InLog.FullLogContent);

					IEnumerable<AutomationTestResult> TotalTests = Parser.GetResults();
					IEnumerable<AutomationTestResult> FailedTests = TotalTests.Where(R => !R.Passed);

					// Tests failed so list that as our primary cause of failure
					if (FailedTests.Any())
					{
						ExitReason = string.Format("{0} of {1} test(s) failed", FailedTests.Count(), TotalTests.Count());
						ExitCode = -1;
						return UnrealProcessResult.TestFailure;
					}

					// If no tests were run then that's a failure (possibly a bad RunTest argument?)
					if (!TotalTests.Any())
					{
						ExitReason = "No tests were executed!";
						ExitCode = -1;
						return UnrealProcessResult.TestFailure;
					}
				}
			}

			return UnrealResult;
		}

		/// <summary>
		/// Optional function that is called on test completion and gives an opportunity to create a report
		/// </summary>
		/// <param name="Result"></param>
		public override ITestReport CreateReport(TestResult Result)
		{
			ITestReport Report = null;
			if (GetConfiguration() is AutomationTestConfig)
			{
				var Config = GetConfiguration() as AutomationTestConfig;
				// Save test result data for Horde build system
				bool WriteTestResultsForHorde = Config.WriteTestResultsForHorde;
				if (WriteTestResultsForHorde)
				{
					if (Config.SimpleHordeReport)
					{
						Report = base.CreateReport(Result);
					}
					else
					{
						string ReportPath = Config.ReportExportPath;
						if (!string.IsNullOrEmpty(ReportPath))
						{
							Report = CreateUnrealEngineTestPassReport(ReportPath, Config.ReportURL);
						}
					}
				}

				string TelemetryDirectory = Config.TelemetryDirectory;
				if (Report != null && !string.IsNullOrEmpty(TelemetryDirectory) && Directory.Exists(TelemetryDirectory))
				{
					if (Report is ITelemetryReport Telemetry)
					{
						UnrealAutomationTelemetry.LoadOutputsIntoReport(TelemetryDirectory, Telemetry);
					}
					else
					{
						Log.Warning("Publishing Telemetry is requested but '{0}' does not support telemetry input.", Report.GetType().FullName);
					}
				}
			}

			return Report;
		}

		/// <summary>
		/// Override the summary report so we can create a custom summary with info about our tests and
		/// a link to the reports
		/// </summary>
		/// <returns></returns>
		protected override string GetTestSummaryHeader()
		{
			const int kMaxErrorsOrWarningsToDisplay = 5;

			MarkdownBuilder MB = new MarkdownBuilder(base.GetTestSummaryHeader());

			// Everything we need is in the editor artifacts
			var EditorRole = RoleResults.Where(R => R.Artifacts.SessionRole.RoleType == UnrealTargetRole.Editor).FirstOrDefault();

			if (EditorRole != null)
			{
				// Parse automaton info from the log (TODO - use the json version)
				AutomationLogParser Parser = new AutomationLogParser(EditorRole.LogSummary.FullLogContent);

				// Filter our tests into categories
				IEnumerable<AutomationTestResult> AllTests = Parser.GetResults();
				IEnumerable<AutomationTestResult> IncompleteTests = AllTests.Where(R => !R.Completed);
				IEnumerable<AutomationTestResult> FailedTests = AllTests.Where(R => R.Completed && !R.Passed);
				IEnumerable<AutomationTestResult> TestsWithWarnings = AllTests.Where(R => R.Completed && R.Passed && R.WarningEvents.Any());

				// If there were abnormal exits then look only at the incomplete tests to avoid confusing things.
				if (GetRolesThatExitedAbnormally().Any())
				{
					if (AllTests.Count() == 0)
					{
						MB.H3("Error: No tests were executed.");
					}
					else if (IncompleteTests.Count() > 0)
					{
						MB.H3("Error: The following test(s) were incomplete:");

						foreach (AutomationTestResult Result in IncompleteTests)
						{
							MB.H4(string.Format("{0}", Result.FullName));
							MB.UnorderedList(Result.WarningAndErrorEvents.Distinct());
						}
					}
				}
				else
				{
					if (AllTests.Count() == 0)
					{
						MB.H3("Error: No tests were executed.");

						IEnumerable<UnrealLog.LogEntry> WarningsAndErrors = Parser.AutomationWarningsAndErrors;

						if (WarningsAndErrors.Any())
						{
							MB.UnorderedList(WarningsAndErrors.Select(E => E.ToString()));
						}
						else
						{
							MB.Paragraph("Unknown failure.");
						}
					}
					else
					{

						Func<string, string> SantizeLineForHorde = (L) =>
						{
							L = L.Replace(": Error: ", ": Err: ");
							L = L.Replace(": Warning: ", ": Warn: ");
							L = L.Replace("LogAutomationController: ", "");

							return L;
						};

						// Now list the tests that failed
						if (FailedTests.Count() > 0)
						{
							MB.H3("The following test(s) failed:");

							foreach (AutomationTestResult Result in FailedTests)
							{
								// only show the last N items
								IEnumerable<string> Events = Result.Events.Distinct();

								if (Events.Count() > kMaxErrorsOrWarningsToDisplay)
								{
									Events = Events.Skip(Events.Count() - kMaxErrorsOrWarningsToDisplay);
								}

								MB.H4(string.Format("Error: Test '{0}' failed", Result.DisplayName));
								MB.Paragraph("FullName: " + Result.TestName);
								MB.UnorderedList(Events.Distinct().Select(E => SantizeLineForHorde(E)));
							}
						}

						if (TestsWithWarnings.Count() > 0)
						{
							MB.H3("The following test(s) completed with warnings:");

							foreach (AutomationTestResult Result in TestsWithWarnings)
							{
							// only show the last N items
							IEnumerable<string> WarningEvents = Result.WarningEvents.Distinct();

							if (WarningEvents.Count() > kMaxErrorsOrWarningsToDisplay)
							{
								WarningEvents = WarningEvents.Skip(WarningEvents.Count() - kMaxErrorsOrWarningsToDisplay);
							}

							MB.H4(string.Format("Warning: Test '{0}' completed with warnings", Result.DisplayName));
							MB.Paragraph("FullName: " + Result.TestName);
							MB.UnorderedList(WarningEvents.Distinct().Select(E => SantizeLineForHorde(E)));
							}
						}

						if (IncompleteTests.Count() > 0)
						{
							MB.H3("The following test(s) timed out or did not run:");

							foreach (AutomationTestResult Result in IncompleteTests)
							{
								// only show the last N items
								IEnumerable<string> WarningAndErrorEvents = Result.WarningAndErrorEvents.Distinct();

								if (WarningAndErrorEvents.Count() > kMaxErrorsOrWarningsToDisplay)
								{
									WarningAndErrorEvents = WarningAndErrorEvents.Skip(WarningAndErrorEvents.Count() - kMaxErrorsOrWarningsToDisplay);
								}

								MB.H4(string.Format("Error: Test '{0}' did not run or complete", Result.DisplayName));
								MB.Paragraph("FullName: " + Result.TestName);
								MB.UnorderedList(WarningAndErrorEvents.Distinct().Select(E => SantizeLineForHorde(E)));
							}
						}

						// show a brief summary at the end where it's most visible
						List<string> TestSummary = new List<string>();

						int PassedTests = AllTests.Count() - (FailedTests.Count() + IncompleteTests.Count());
						int TestsPassedWithoutWarnings = PassedTests - TestsWithWarnings.Count();

						TestSummary.Add(string.Format("{0} Test(s) Requested", AllTests.Count()));

						// Print out a summary of each category of result
						if (TestsPassedWithoutWarnings > 0)
						{
							TestSummary.Add(string.Format("{0} Test(s) Passed", TestsPassedWithoutWarnings));
						}

						if (TestsWithWarnings.Count() > 0)
						{
							TestSummary.Add(string.Format("{0} Test(s) Passed with warnings", TestsWithWarnings.Count()));
						}

						if (FailedTests.Count() > 0)
						{
							TestSummary.Add(string.Format("{0} Test(s) Failed", FailedTests.Count()));
						}

						if (IncompleteTests.Count() > 0)
						{
							TestSummary.Add(string.Format("{0} Test(s) didn't complete", IncompleteTests.Count()));
						}

						MB.H3("Summary");
						MB.UnorderedList(TestSummary);
					}
				}

				if (EditorRole.LogSummary.EngineInitialized)
				{
					// Use the paths from the report. If we passed these in they should be the same, and if not
					// they'll be valid defaults
					string AutomationReportPath = Parser.AutomationReportPath;
					string AutomationReportURL = Parser.AutomationReportURL;
					if (GetConfiguration() is AutomationTestConfig Config)
					{
						if (string.IsNullOrEmpty(AutomationReportPath))
						{
							AutomationReportPath = Config.ReportExportPath;
						}
						if (string.IsNullOrEmpty(AutomationReportURL))
						{
							AutomationReportURL = Config.ReportURL;
						}
					}
					if (!string.IsNullOrEmpty(AutomationReportPath) || !string.IsNullOrEmpty(AutomationReportURL))
					{
						MB.H3("Links");

						if (string.IsNullOrEmpty(AutomationReportURL) == false)
						{
							MB.Paragraph(string.Format("View results here: {0}", AutomationReportURL));
						}

						if (string.IsNullOrEmpty(AutomationReportPath) == false)
						{
							MB.Paragraph(string.Format("Open results in UnrealEd from {0}", AutomationReportPath));
						}
					}
				}
			}

			return MB.ToString();
		}

		/// <summary>
		/// Returns Errors found during tests. We call the base version to get standard errors then
		/// Add on any errors seen in tests
		/// </summary>
		public override IEnumerable<string> GetErrors()
		{
			List<string> AllErrors = new List<string>(base.GetErrors());

			foreach (var Role in GetRolesThatFailed())
			{
				if (Role.Artifacts.SessionRole.RoleType == UnrealTargetRole.Editor)
				{
					AutomationLogParser Parser = new AutomationLogParser(Role.LogSummary.FullLogContent);
					AllErrors.AddRange(
						Parser.GetResults().Where(R => !R.Passed)
							.SelectMany(R => R.Events
								.Where(E => E.ToLower().Contains("error"))
								.Distinct().Select(E => string.Format("[test={0}] {1}", R.DisplayName, E))
							)
						);
				}
			}

			return AllErrors;
		}

		/// <summary>
		/// Returns warnings found during tests. We call the base version to get standard warnings then
		/// Add on any errors seen in tests
		/// </summary>
		public override IEnumerable<string> GetWarnings()
		{
			List<string> AllWarnings = new List<string>(base.GetWarnings());

			if (SessionArtifacts == null)
			{
				return AllWarnings;
			}

			foreach (var Role in RoleResults)
			{
				if (Role.Artifacts.SessionRole.RoleType == UnrealTargetRole.Editor)
				{
					AutomationLogParser Parser = new AutomationLogParser(Role.LogSummary.FullLogContent);
					AllWarnings.AddRange(
						Parser.GetResults()
							.SelectMany(R => R.Events
								.Where(E => E.ToLower().Contains("warning"))
								.Distinct().Select(E => string.Format("[test={0}] {1}", R.DisplayName, E))
							)
						);
				}
			}

			return AllWarnings;
		}
	}

}
